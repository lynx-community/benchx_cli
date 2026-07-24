#ifndef CLI_BENCHX_PERFORMANCE_APIS
#define CLI_BENCHX_PERFORMANCE_APIS

// Walltime instrument controls, driven by the run loop in benchx_cli.cc.
// All are no-ops unless the process runs under
// `codspeed run --mode walltime` (CODSPEED_RUNNER_MODE=walltime).

// True when the walltime instrument is active for this process.
bool codspeed_walltime_active();

// Target number of recorded rounds per benchmark uri. Extra samples beyond the
// target are dropped, which bounds memory and gives a stable round count even
// for harnesses (e.g. tinybench) that loop the region many times per run. The
// first sample of each uri is always discarded as warmup before recording
// starts.
void codspeed_walltime_set_target_rounds(int rounds);

// True once every observed uri has recorded its target rounds, so the run loop
// can stop re-running the bundle early. False when inactive or before any
// sample has been recorded.
bool codspeed_walltime_saturated();

#endif  // CLI_BENCHX_PERFORMANCE_APIS
