// ---------------------------------------------------------------------------
// utils/video/video_job_dry_run.cpp — Phase 3: dry-run a VideoJobPlan
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int dry_run_video_job(const VideoJobPlan& plan) {
    const int total = static_cast<int>(plan.end_exclusive - plan.start);

    spdlog::info("[dry-run] Composition: {}", plan.comp_id);
    spdlog::info("[dry-run]   Resolution: {}×{}",
                 plan.comp->width(), plan.comp->height());
    spdlog::info("[dry-run]   Frame range: {} – {} ({} frames)",
                 plan.start, plan.end_exclusive, total);
    spdlog::info("[dry-run]   FPS: {}", plan.export_options.fps);
    spdlog::info("[dry-run]   Duration: {:.1f}s",
                 static_cast<double>(total) / plan.export_options.fps);
    spdlog::info("[dry-run]   Output: {}", plan.export_options.output);
    spdlog::info("[dry-run]   FFmpeg mode: {}", plan.export_options.ffmpeg_mode);
    spdlog::info("[dry-run]   SSAA: {}×", plan.settings.ssaa_factor);

    // Try to build the render graph to detect errors early
    try {
        auto renderer = create_renderer(*plan.registry, plan.settings);
        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
        if (sw_renderer) {
            spdlog::info("[dry-run]   Backend: SoftwareRenderer");
        }
        cache::NodeCache node_cache;
        auto fb = graph::render_composition_frame(
            *renderer, node_cache, plan.settings, plan.registry,
            nullptr, *plan.comp, plan.start);
        if (!fb) {
            spdlog::warn("[dry-run]   First frame render returned null");
        } else {
            spdlog::info("[dry-run]   First frame render: OK ({}×{})",
                         fb->width(), fb->height());
        }
    } catch (const std::exception& e) {
        spdlog::error("[dry-run]   Render error: {}", e.what());
        return 1;
    }

    spdlog::info("[dry-run] ✅ Composition is valid — no rendering performed.");
    return 0;
}

} // namespace chronon3d::cli
