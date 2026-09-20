#pragma once

#include "daemon_render_concurrency.hpp"

#include <cstdint>

namespace chronon3d::cli {

/// Resolve the execution-domain verdict for one normalized job without owning
/// transport state, mutexes or scheduler reservations.
[[nodiscard]] RenderPolicyDecision decide_render_job_admission(
    bool parallel_disjoint,
    std::uint64_t estimated_vram_bytes);

} // namespace chronon3d::cli
