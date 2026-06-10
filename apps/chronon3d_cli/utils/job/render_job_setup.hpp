#pragma once

// ---------------------------------------------------------------------------
// Render Job Setup Phase
//
// Extracts the initialisation sub-steps from the monolithic
// execute_render_job() — asset mounting, counter zeroing, renderer creation,
// warmup, counter save/restore, and telemetry-store clearing — into a single
// reusable setup helper.
// ---------------------------------------------------------------------------

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/system_metrics.hpp>

#include "render_job.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace chronon3d::cli {

/// Accumulated state produced by the setup phase of a render job.
struct RenderJobSetupResult {
    std::shared_ptr<SoftwareRenderer> renderer;
    SystemMetricsCollector sys_metrics;
    std::string job_started_iso;
    std::chrono::steady_clock::time_point wall_t0;
    std::chrono::steady_clock::time_point setup_t0;
    std::chrono::steady_clock::time_point setup_t1;

    // Warmup counter baselines preserved across the counter reset
    uint64_t saved_fb_alloc{0};
    uint64_t saved_fb_reuses{0};
    uint64_t saved_fb_bytes{0};
    uint64_t saved_fb_peak{0};
};

/// Initialise the asset registry, create the renderer, run optional warmup,
/// reset atomic counters, and clear per-event telemetry stores.
/// Populates @p out with the setup result — check renderer via operator bool().
void setup_render_job(const CompositionRegistry& registry,
                      const RenderJobPlan& plan,
                      RenderJobSetupResult& out);

} // namespace chronon3d::cli
