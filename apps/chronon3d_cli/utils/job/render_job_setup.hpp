#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>

#include "render_job.hpp"

#include <memory>
#include <string>

namespace chronon3d::cli {

struct RenderJobSetupResult {
    std::shared_ptr<SoftwareRenderer> renderer;
    SystemMetricsCollector sys_metrics;
    std::string job_started_iso;
    profiling::Clock::time_point wall_t0;
    profiling::Clock::time_point setup_t0;
    profiling::Clock::time_point setup_t1;

    uint64_t saved_fb_alloc{0};
    uint64_t saved_fb_reuses{0};
    uint64_t saved_fb_bytes{0};
    uint64_t saved_fb_peak{0};
};

/// Initialise renderer/runtime services from an immutable canonical job.
/// Config is copied into the renderer; RenderJob remains reusable and
/// inspectable after execution.
void setup_render_job(const CompositionRegistry& registry,
                      const RenderJob& job,
                      RenderJobSetupResult& out);

} // namespace chronon3d::cli
