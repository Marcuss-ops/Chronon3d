#include "render_job_detail.hpp"
#include "render_job_finalize.hpp"
#include "render_job_loop.hpp"
#include "render_job_setup.hpp"
#include "../video/video_job_plan.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <exception>

namespace chronon3d::cli {

Result<RenderJobOutput, RenderJobError> execute_render_job(RenderJob& job) {
    if (!job.registry) {
        return RenderJobError{
            RenderJobErrorCode::InvalidJob,
            "RenderJob has no CompositionRegistry"};
    }
    if (!job.comp) {
        return RenderJobError{
            RenderJobErrorCode::InvalidJob,
            "RenderJob has no resolved Composition"};
    }

    if (job.mode == RenderMode::Video) {
        if (!validate_video_job(job)) {
            return RenderJobError{
                RenderJobErrorCode::ValidationFailed,
                "Video RenderJob validation failed for '" + job.comp_id + "'"};
        }

        const int rc = job.video_settings.dry_run
            ? dry_run_video_job(job)
            : execute_video_job(job);
        if (rc != 0) {
            return RenderJobError{
                RenderJobErrorCode::RenderFailed,
                "Video render failed for composition '" + job.comp_id + "'"};
        }

        const int frames = static_cast<int>(
            job.last_frame.integral() - job.first_frame.integral() + 1);
        return RenderJobOutput{
            .mode = RenderMode::Video,
            .output = job.output,
            .frames_written = job.video_settings.dry_run ? 0 : frames,
        };
    }

    try {
        RenderJobSetupResult setup;
        setup_render_job(*job.registry, job, setup);
        if (!setup.renderer) {
            return RenderJobError{
                RenderJobErrorCode::SetupFailed,
                "Failed to create renderer for composition '" + job.comp_id + "'"};
        }

        const Frame start = job.mode == RenderMode::Still
            ? job.still_frame
            : job.first_frame;
        const Frame end = job.mode == RenderMode::Still
            ? job.still_frame
            : job.last_frame;
        const Frame step = Frame{std::max<std::int64_t>(
            1, job.frame_step.integral())};

        spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
                     job.comp_id, start, end, step,
                     chronon3d::is_motion_blur_active(job.settings.motion_blur)
                         ? fmt::format(" [MB {}smp {:.0f}°/{:.0f}°]",
                                       job.settings.motion_blur.samples,
                                       job.settings.motion_blur.shutter_angle_deg,
                                       job.settings.motion_blur.shutter_phase_deg)
                         : "",
                     job.settings.ssaa_factor > 1.0f
                         ? fmt::format(" [SSAA {:.1f}x]", job.settings.ssaa_factor)
                         : "");

        setup.sys_metrics.sample_cpu_start();
        auto loop = run_render_job_loop(job, *setup.renderer);

        const bool ok = finalize_render_job(
            job, setup, loop.telemetry_frames,
            loop.total_render_ms, loop.total_encode_ms, loop.frames_written,
            loop.ok, loop.loop_start, loop.loop_end);

        if (!ok) {
            return RenderJobError{
                RenderJobErrorCode::RenderFailed,
                "Render failed for composition '" + job.comp_id + "'"};
        }

        return RenderJobOutput{
            .mode = job.mode,
            .output = job.output,
            .frames_written = loop.frames_written,
        };
    } catch (const std::exception& e) {
        return RenderJobError{
            RenderJobErrorCode::RenderFailed,
            e.what()};
    }
}

bool execute_render_job(const CompositionRegistry& registry, RenderJob& job) {
    job.registry = &registry;
    return static_cast<bool>(execute_render_job(job));
}

} // namespace chronon3d::cli
