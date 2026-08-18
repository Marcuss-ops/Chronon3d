#pragma once

#include <chronon3d/core/profiling/profiling.hpp>

namespace chronon3d::cli {

/// Anchors the steady clock at the very first line of `main()`, so callers
/// can report process startup (CLI boot + argument parsing) as a measured
/// wall time instead of a misleading zero or a hand-waved estimate.
///
/// The anchor is a function-local `static const` time point: it is written
/// exactly once (on first call, before any CLI work runs) and read-only
/// afterwards. It is intentionally not a mutable registry/global.
inline profiling::Clock::time_point process_start_time() {
    static const profiling::Clock::time_point t0 = profiling::now();
    return t0;
}

/// Call at the top of `main()` so `process_start_time()` is captured before
/// CLI boot work (TBB init, registry registration, argument parsing).
inline void record_process_start() {
    (void)process_start_time();
}

} // namespace chronon3d::cli
