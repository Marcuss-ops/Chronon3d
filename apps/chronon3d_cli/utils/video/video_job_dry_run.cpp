#include "video_export_support.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <chronon3d/runtime/render_preparation.hpp>
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
                 job.metadata.width, job.metadata.height);
    spdlog::info("[dry-run]   Frame range: {} – {} inclusive ({} frames)",
                 job.first_frame, job.last_frame, total);
    const auto fps = chronon3d::FrameRate{
        job.video_settings.fps_num, job.video_settings.fps_den};
    spdlog::info("[dry-run]   FPS: {}/{}", fps.numerator, fps.denominator);
    spdlog::info("[dry-run]   Duration: {:.1f}s",
                 fps.to_seconds(total));
    spdlog::info("[dry-run]   Output: {}", job.output);
    spdlog::info("[dry-run]   Sink: {} ({})",
                 job.video_settings.sink_type,
                 job.video_settings.ffmpeg_mode);
    spdlog::info("[dry-run]   SSAA: {}×", job.settings.ssaa_factor);

    try {
        auto renderer = create_renderer(
            *job.registry, job.settings, job.execution.config, job.execution.assets_root);

        const auto preparation = runtime::prepare_render(
            renderer.get(), *job.compiled,
            runtime::RenderPreparationOptions{
                .warmup_renderer = false,
                .reference_frame = job.first_frame,
            });
        if (!preparation.ok()) {
            spdlog::error("[dry-run] Render preparation FAILED:\n{}",
                          preparation.diagnostic());
            return 1;
        }

        spdlog::info("[dry-run]   Backend: SoftwareRenderer");
        auto fb = renderer->render_compiled(
            *job.compiled, job.first_frame);
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
