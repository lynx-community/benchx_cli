#include "cli/benchx/performance_apis.h"

#include <chrono>

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

static bool is_running_on_valgrind = []() {
  if (running_on_valgrind()) {
    callgrind_dump_stats_at((const uint8_t*)"Metadata: codspeed-cpp 1.2.0");
    return true;
  }
  return false;
}();

LEPUSValue perf_start_benchmark(LEPUSContext* ctx, LEPUSValueConst this_val,
                                int argc, LEPUSValueConst* argv) {
  if (is_running_on_valgrind) {
    callgrind_zero_stats();
    callgrind_start_instrumentation();
  }
  return LEPUS_UNDEFINED;
}

LEPUSValue perf_stop_benchmark(LEPUSContext* ctx, LEPUSValueConst this_val,
                               int argc, LEPUSValueConst* argv) {
  if (is_running_on_valgrind) {
    callgrind_stop_instrumentation();
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
