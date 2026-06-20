#pragma once

// ---------------------------------------------------------------------------
// core/scheduler/scheduler_mode.hpp
//
// SchedulerMode enum and its env-parsing helpers.  Header-light (no TBB
// dependency) so `core/config.hpp` can expose `SchedulerConfig::mode()`
// without dragging tbb::task_arena into every translation unit that
// touches the chrono config.
// ---------------------------------------------------------------------------

#include <string_view>

namespace chronon3d {

enum class SchedulerMode {
    Sequential,   ///< arena(1) — caps nested tbb::parallel_for
    TbbAutomatic, ///< arena(default) — TBB infers local slot count
    TbbFixed,     ///< arena(N)    — reproducible for benchmarks (default in PR-B)
};

std::string_view scheduler_mode_name(SchedulerMode mode) noexcept;
bool parse_scheduler_mode(std::string_view text, SchedulerMode& out) noexcept;

} // namespace chronon3d
