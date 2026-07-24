#include "cli/benchx/performance_apis.h"

#include <chrono>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "quickjs/include/trace-gc.h"

extern "C" {
#include "callgrind.h"
#include "quickjs/include/quickjs.h"
#include "valgrind.h"
}

// clang-format off
inline __attribute__((always_inline)) uint8_t running_on_valgrind() { return RUNNING_ON_VALGRIND > 0; }

inline __attribute__((always_inline)) void callgrind_dump_stats() { CALLGRIND_DUMP_STATS; }

inline __attribute__((always_inline)) void callgrind_dump_stats_at(uint8_t const* pos_str) { CALLGRIND_DUMP_STATS_AT(pos_str); }

inline __attribute__((always_inline)) void callgrind_zero_stats() { CALLGRIND_ZERO_STATS; }

inline __attribute__((always_inline)) void callgrind_start_instrumentation() { CALLGRIND_START_INSTRUMENTATION; }

inline __attribute__((always_inline)) void callgrind_stop_instrumentation() { CALLGRIND_STOP_INSTRUMENTATION; }
// clang-format on

// RUNNING_ON_VALGRIND can return 0 even under valgrind in an optimized (-O2)
// build, so also honour CODSPEED_RUNNER_MODE (env, robust under -O2 like the
// walltime path). Callgrind dumps are no-ops when not actually under valgrind.
static bool codspeed_mode_is_simulation() {
  const char* mode = std::getenv("CODSPEED_RUNNER_MODE");
  return mode != nullptr
      && (std::string(mode) == "simulation" || std::string(mode) == "instrumentation");
}

static bool is_running_on_valgrind = []() {
  if (running_on_valgrind() || codspeed_mode_is_simulation()) {
    callgrind_dump_stats_at((const uint8_t*)"Metadata: codspeed-cpp 1.2.0");
    return true;
  }
  return false;
}();

// ---------------------------------------------------------------------------
// Walltime instrument.
//
// `codspeed run --mode walltime` runs this process WITHOUT valgrind, so every
// callgrind path above is a no-op and nothing gets reported. Instead we time
// each benchmarked region with steady_clock, accumulate per-uri samples, and
// on exit write them in CodSpeed's walltime format to
// $CODSPEED_PROFILE_FOLDER/results/<pid>.json (matching @codspeed/core's
// writeWalltimeResults). The JS harness call sites are unchanged: start/stop
// wrap the region and setExecutedBenchmark(uri) commits one sample.
// ---------------------------------------------------------------------------
namespace codspeed_walltime {

// Leading samples discarded per uri to skip cold start (first template parse,
// JIT, allocator warmup). The run loop re-uses one process, so warming up once
// per uri is enough.
constexpr long kWarmupRounds = 1;

struct State {
  bool enabled = false;
  size_t target = 0;  // recorded rounds per uri before extras are dropped
  std::chrono::steady_clock::time_point start{};
  double pending_ns = 0;
  std::map<std::string, std::vector<double>> samples;  // uri -> round times (ns)
  std::map<std::string, long> warmup_iters;            // uri -> discarded rounds
};

inline State& state() {
  static State s;
  return s;
}

inline void begin() { state().start = std::chrono::steady_clock::now(); }

inline void end() {
  const auto d = std::chrono::steady_clock::now() - state().start;
  state().pending_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

inline void commit(const std::string& uri) {
  State& s = state();
  // Discard cold-start samples first so they don't skew the reported stats.
  if (s.warmup_iters[uri] < kWarmupRounds) {
    s.warmup_iters[uri]++;
    return;
  }
  // Cap recorded samples: harnesses like tinybench fire the region many times
  // per run, so without a cap one run could record millions of "rounds".
  if (s.target > 0 && s.samples[uri].size() >= s.target) return;
  s.samples[uri].push_back(s.pending_ns);
}

// True once every observed uri reached the target round count.
inline bool saturated() {
  State& s = state();
  if (!s.enabled || s.target == 0 || s.samples.empty()) return false;
  for (auto& kv : s.samples) {
    if (kv.second.size() < s.target) return false;
  }
  return true;
}

inline double quantile(const std::vector<double>& sorted, double q) {
  if (sorted.empty()) return 0.0;
  if (sorted.size() == 1) return sorted.front();
  const double pos = q * static_cast<double>(sorted.size() - 1);
  const size_t lo = static_cast<size_t>(std::floor(pos));
  const size_t hi = static_cast<size_t>(std::ceil(pos));
  const double frac = pos - static_cast<double>(lo);
  return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

inline std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (const char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default: o += c;
    }
  }
  return o;
}

// Registered with std::atexit; writes one results file for this process.
inline void write_results() {
  State& s = state();
  if (!s.enabled || s.samples.empty()) return;

  const char* folder = std::getenv("CODSPEED_PROFILE_FOLDER");
  const std::string dir =
      folder ? std::string(folder) + "/results" : std::string(".codspeed");
  ::mkdir(dir.c_str(), 0755);  // best-effort; parent created by the runner

  const long pid = static_cast<long>(::getpid());
  const std::string path = dir + "/" + std::to_string(pid) + ".json";
  std::FILE* fp = std::fopen(path.c_str(), "w");
  if (!fp) return;

  std::fprintf(fp,
               "{\n"
               "  \"creator\": { \"name\": \"benchx_cli\", \"version\": "
               "\"1.0.0\", \"pid\": %ld },\n"
               "  \"instrument\": { \"type\": \"walltime\" },\n"
               "  \"benchmarks\": [\n",
               pid);

  bool first = true;
  for (auto& kv : s.samples) {
    std::vector<double> v = kv.second;
    if (v.empty()) continue;
    std::sort(v.begin(), v.end());

    const size_t n = v.size();
    double sum = 0.0;
    for (double x : v) sum += x;
    const double mean = sum / static_cast<double>(n);
    double sq = 0.0;
    for (double x : v) sq += (x - mean) * (x - mean);
    const double stdev =
        n > 1 ? std::sqrt(sq / static_cast<double>(n - 1)) : 0.0;

    const double q1 = quantile(v, 0.25);
    const double median = quantile(v, 0.5);
    const double q3 = quantile(v, 0.75);
    const double iqr = q3 - q1;
    const double lo = q1 - 1.5 * iqr;
    const double hi = q3 + 1.5 * iqr;
    long iqr_outliers = 0;
    long stdev_outliers = 0;
    for (double x : v) {
      if (x < lo || x > hi) iqr_outliers++;
      if (stdev > 0.0 && std::fabs(x - mean) > 3.0 * stdev) stdev_outliers++;
    }

    const std::string uri = json_escape(kv.first);
    const size_t sep = kv.first.rfind("::");
    const std::string name = json_escape(
        sep == std::string::npos ? kv.first : kv.first.substr(sep + 2));

    if (!first) std::fprintf(fp, ",\n");
    first = false;
    std::fprintf(
        fp,
        "    {\n"
        "      \"name\": \"%s\",\n"
        "      \"uri\": \"%s\",\n"
        "      \"config\": { \"warmup_time_ns\": null, "
        "\"min_round_time_ns\": null, \"max_time_ns\": null, "
        "\"max_rounds\": null },\n"
        "      \"stats\": {\n"
        "        \"min_ns\": %.1f, \"max_ns\": %.1f, \"mean_ns\": %.1f, "
        "\"stdev_ns\": %.1f,\n"
        "        \"q1_ns\": %.1f, \"median_ns\": %.1f, \"q3_ns\": %.1f,\n"
        "        \"rounds\": %zu, \"total_time\": %.9f,\n"
        "        \"iqr_outlier_rounds\": %ld, \"stdev_outlier_rounds\": %ld,\n"
        "        \"iter_per_round\": 1, \"warmup_iters\": %ld\n"
        "      }\n"
        "    }",
        name.c_str(), uri.c_str(), v.front(), v.back(), mean, stdev, q1, median,
        q3, n, sum / 1e9, iqr_outliers, stdev_outliers, s.warmup_iters[kv.first]);
  }

  std::fprintf(fp, "\n  ]\n}\n");
  std::fclose(fp);
}

}  // namespace codspeed_walltime

static bool is_walltime = []() {
  const char* mode = std::getenv("CODSPEED_RUNNER_MODE");
  const bool on = mode != nullptr && std::string(mode) == "walltime";
  if (on) {
    codspeed_walltime::state().enabled = true;
    std::atexit(codspeed_walltime::write_results);
  }
  return on;
}();

bool codspeed_walltime_active() { return is_walltime; }

void codspeed_walltime_set_target_rounds(int rounds) {
  codspeed_walltime::state().target = rounds > 0 ? rounds : 0;
}

bool codspeed_walltime_saturated() { return codspeed_walltime::saturated(); }

LEPUSValue perf_start_benchmark(LEPUSContext* ctx, LEPUSValueConst this_val,
                                int argc, LEPUSValueConst* argv) {
  if (is_running_on_valgrind) {
    callgrind_zero_stats();
    callgrind_start_instrumentation();
  } else if (is_walltime) {
    codspeed_walltime::begin();
  }
  return LEPUS_UNDEFINED;
}

LEPUSValue perf_stop_benchmark(LEPUSContext* ctx, LEPUSValueConst this_val,
                               int argc, LEPUSValueConst* argv) {
  if (is_running_on_valgrind) {
    callgrind_stop_instrumentation();
  } else if (is_walltime) {
    codspeed_walltime::end();
  }
  return LEPUS_UNDEFINED;
}

LEPUSValue perf_set_executed_benchmark(LEPUSContext* ctx,
                                       LEPUSValueConst this_val, int argc,
                                       LEPUSValueConst* argv) {
  const char* str;
  size_t len;

  HandleScope scope(ctx);
  str = LEPUS_ToCStringLen2(ctx, &len, argv[0], 0);
  if (is_running_on_valgrind) {
    callgrind_dump_stats_at((const uint8_t*)str);
  } else if (is_walltime) {
    codspeed_walltime::commit(std::string(str, len));
  }
  if (!LEPUS_IsGCMode(ctx)) {
    LEPUS_FreeCString(ctx, str);
  }
  return LEPUS_UNDEFINED;
}

LEPUSValue perf_zero_stats(LEPUSContext* ctx, LEPUSValueConst this_val,
                           int argc, LEPUSValueConst* argv) {
  if (is_running_on_valgrind) {
    callgrind_zero_stats();
  }
  return LEPUS_UNDEFINED;
}

static auto time0 = std::chrono::steady_clock::now();
LEPUSValue perf_now(LEPUSContext* ctx, LEPUSValueConst this_val, int argc,
                    LEPUSValueConst* argv) {
  const auto time = std::chrono::steady_clock::now() - time0;
  return LEPUS_NewFloat64(
      ctx, std::chrono::duration_cast<std::chrono::nanoseconds>(time).count() /
               1000000.0);
}

static const LEPUSCFunctionListEntry codspeed_funcs[] = {
    LEPUS_CFUNC_DEF("startBenchmark", 0, perf_start_benchmark),
    LEPUS_CFUNC_DEF("stopBenchmark", 0, perf_stop_benchmark),
    LEPUS_CFUNC_DEF("setExecutedBenchmark", 1, perf_set_executed_benchmark),
    LEPUS_CFUNC_DEF("zeroStats", 1, perf_zero_stats),
};

static const LEPUSCFunctionListEntry js_perf_funcs[] = {
    LEPUS_CFUNC_DEF("now", 0, perf_now),
};

static const LEPUSCFunctionListEntry obj[] = {
    LEPUS_OBJECT_DEF("Codspeed", codspeed_funcs,
                     sizeof(codspeed_funcs) / sizeof(codspeed_funcs[0]),
                     LEPUS_PROP_WRITABLE | LEPUS_PROP_CONFIGURABLE),
    LEPUS_OBJECT_DEF("performance", js_perf_funcs,
                     sizeof(js_perf_funcs) / sizeof(js_perf_funcs[0]),
                     LEPUS_PROP_WRITABLE | LEPUS_PROP_CONFIGURABLE),
};

extern "C" {
void LEPUS_AddExtraIntrinsicObjects(LEPUSContext* ctx) {
  LEPUSValue global = LEPUS_GetGlobalObject(ctx);
  LEPUS_SetPropertyFunctionList(ctx, global, obj, sizeof(obj) / sizeof(obj[0]));
  if (!LEPUS_IsGCMode(ctx)) {
    LEPUS_FreeValue(ctx, global);
  }
}
}
