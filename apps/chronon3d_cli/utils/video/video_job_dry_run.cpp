#include "video_export_support.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int dry_run_video_job(const RenderJob& job) {
    if (!validate_video_job(job)) {
        return 1;
    }

    const int total = static_cast<int>(
        job.last_frame.integral() - job.first_frame.integral() + 1);

    spdlog::info("[dry-run] Composition: {}", job.comp_id);
    spdlog::info("[dry-run]   Resolution: {}×{}",
                 job.comp->width(), job.comp->height());
    spdlog::info("[dry-run]   Frame range: {} – {} inclusive ({} frames)",
                 job.first_frame, job.last_frame, total);
    spdlog::info("[dry-run]   FPS: {}", job.video_settings.fps);
    spdlog::info("[dry-run]   Duration: {:.1f}s",
                 static_cast<double>(total) / job.video_settings.fps);
    spdlog::info("[dry-run]   Output: {}", job.output);
    spdlog::info("[dry-run]   Sink: {} ({})",
                 job.video_settings.sink_type,
                 job.video_settings.ffmpeg_mode);
    spdlog::info("[dry-run]   SSAA: {}×", job.settings.ssaa_factor);

    try {
        auto renderer = create_renderer(*job.registry, job.settings);

        Scene scene = job.comp->evaluate(job.first_frame);
        auto preflight_result = AssetPreflightResolver::check(
            scene, renderer->runtime().resolver(),
            PreflightMode::FullComposition);
        if (!preflight_result.ok()) {
            std::string text =
                format_preflight_issues_text(preflight_result.issues);
            spdlog::error("[dry-run] Asset preflight FAILED:\n{}", text);
            return 1;
        }

        spdlog::info("[dry-run]   Backend: SoftwareRenderer");
        cache::NodeCache node_cache;
        auto fb = graph::render_composition_frame(
            renderer->backend(), node_cache, job.settings, job.registry,
            nullptr, *job.comp, job.first_frame);
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

    spdlog::info("[dry-run] Composition is valid — no rendering performed.");
    return 0;
}

} // namespace chronon3d::cli
