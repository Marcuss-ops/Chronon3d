#include "render_job_detail.hpp"
#include "render_job_setup.hpp"
#include "render_job_finalize.hpp"
#include "render_job_loop.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan) {
    // ═══════════════════════════════════════════════════════════════════
    // PHASE 1 — Setup:  asset mount, renderer creation, warmup,
    //                    counter reset, telemetry-store clear
    // ═══════════════════════════════════════════════════════════════════
    RenderJobSetupResult setup;
    setup_render_job(registry, plan, setup);
    if (!setup.renderer) {
        spdlog::error("Failed to create renderer for composition '{}'", plan.comp_id);
        return false;
    }

    auto& renderer = setup.renderer;

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
                 plan.comp_id, plan.range.start, plan.range.end, plan.range.step,
                 plan.settings.motion_blur.enabled
                     ? fmt::format(" [MB {}smp {:.0f}°/{:.0f}°]",
                                   plan.settings.motion_blur.samples,
                                   plan.settings.motion_blur.shutter_angle_deg,
                                   plan.settings.motion_blur.shutter_phase_deg)
                     : "",
                 plan.settings.ssaa_factor > 1.0f
                     ? fmt::format(" [SSAA {:.1f}x]", plan.settings.ssaa_factor)
                     : "");

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 — Render Loop:  double-buffered or single-frame fallback
    // ═══════════════════════════════════════════════════════════════════
    // Capture CPU baseline before the render loop
    setup.sys_metrics.sample_cpu_start();

    auto loop = run_render_job_loop(plan, *renderer);

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 3 — Finalize:  telemetry collection, report generation
    // ═══════════════════════════════════════════════════════════════════
    return finalize_render_job(plan, setup, loop.telemetry_frames,
                               loop.total_render_ms, loop.total_encode_ms, loop.frames_written,
                               loop.ok, loop.loop_start, loop.loop_end);
}

} // namespace chronon3d::cli
