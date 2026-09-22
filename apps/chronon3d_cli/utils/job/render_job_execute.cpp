#include "render_job_detail.hpp"
#include "render_job_finalize.hpp"
#include "render_job_loop.hpp"
#include "render_job_setup.hpp"

#ifdef CHRONON3D_HAS_CLI_VIDEO_EXPORT
#include "../video/video_export_support.hpp"
#include <chronon3d/core/cancellation_token.hpp>
#endif

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/tracing/trace_session.hpp>
#ifdef CHRONON3D_ENABLE_VERIFICATION
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/verification/render_receipt.hpp>
#endif

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_VERIFICATION
void write_render_job_receipt(const RenderJob& job) {
    if (!job.execution.report || job.output.empty() || job.program) return;

    verification::RenderReceiptInput input;
    input.job_id = job.comp_id;
    input.git_sha = chronon3d::telemetry::TelemetryManager::get_git_commit();
    input.backend = job.video_settings.hardware_encoder == "none"
        ? "software" : "vulkan";
    input.width = job.metadata.width;
    input.height = job.metadata.height;
    input.fps_num = job.video_settings.fps_num;
    input.fps_den = job.video_settings.fps_den;
    input.frames = job.mode == RenderMode::Video
        ? static_cast<std::int64_t>(job.last_frame.integral() -
                                    job.first_frame.integral() + 1)
        : static_cast<std::int64_t>(job.frame_count().integral());
    input.requested_codec = job.video_settings.codec.empty()
        ? "auto" : job.video_settings.codec;
    input.has_audio_tracks = false;
    const auto receipt = verification::build_render_receipt(
        input, job.output,
        job.mode == RenderMode::Video);
    const auto receipt_path = verification::write_render_receipt(
        receipt, job.output);
    spdlog::info(
        "receipt: {} verification={} copy_eligible={}",
        receipt_path.string(),
        receipt.verification.resolved_policy,
        receipt.copy_eligible ? "true" : "false");
}
#else
void write_render_job_receipt(const RenderJob&) {}
#endif

namespace {

/// Job-scoped Perfetto trace session (--trace <path>.pftrace).  Starts before
/// the render; drains the in-memory RING_BUFFER into the file ONLY at job end
/// (destructor) — zero trace I/O on the render hot path.  Level → buffer size
/// per plan §14: pipeline = 32 MiB, nodes/full = 64 MiB.
class JobTraceSession {
public:
    explicit JobTraceSession(const RenderJob& job) {
        if (job.execution.trace_output.empty()) return;
        chronon3d::trace::TraceOptions options;
        options.enabled = true;
        options.output = job.execution.trace_output;
        options.level = trace_level_from_name(job.execution.trace_level);
        options.buffer_mb =
            options.level == chronon3d::trace::TraceLevel::kPipeline ? 32U : 64U;
        const auto started = session_.start(options);
        if (!started) {
            spdlog::warn("[trace] failed to start trace session: {}",
                         static_cast<int>(started.error()));
            return;
        }
        output_ = options.output;
        active_ = true;
    }

    ~JobTraceSession() {
        if (!active_) return;
        try {
            const auto finished = session_.finish();
            if (!finished) {
                spdlog::warn("[trace] failed to write trace at job end: {}",
                             static_cast<int>(finished.error()));
            } else {
                spdlog::info("[trace] timeline written to {} at job end",
                             output_.string());
            }
        } catch (const std::exception& e) {
            spdlog::warn("[trace] exception while writing trace: {}", e.what());
        }
    }

    JobTraceSession(const JobTraceSession&) = delete;
    JobTraceSession& operator=(const JobTraceSession&) = delete;

private:
    static chronon3d::trace::TraceLevel trace_level_from_name(
        const std::string& name) {
        if (name == "nodes") return chronon3d::trace::TraceLevel::kNodes;
        if (name == "full") return chronon3d::trace::TraceLevel::kFull;
        return chronon3d::trace::TraceLevel::kPipeline;
    }

    bool active_{false};
    std::filesystem::path output_;
    chronon3d::trace::TraceSession session_;
};

}  // namespace

Result<RenderJobOutput, RenderJobError> execute_render_job(const RenderJob& job) {
    return execute_render_job(job, {}, {});
}

Result<RenderJobOutput, RenderJobError> execute_render_job(
    const RenderJob& job,
    std::shared_ptr<SoftwareRenderer> warm_renderer,
    std::shared_ptr<media::VideoJobExecutionContext> execution) {
    if (!job.registry) {
        return make_render_job_error(
            RenderJobErrorCode::InvalidJob,
            "RenderJob has no CompositionRegistry");
    }
    if (!job.program && (!job.compiled || !job.compiled->composition)) {
        return make_render_job_error(
            RenderJobErrorCode::InvalidJob,
            "RenderJob has neither a compiled program nor a compiled composition");
    }

    // --trace: job-scoped Perfetto session covering every render mode.  The
    // .pftrace file is written only when this guard is destroyed (job end),
    // including failure paths — never during the render hot path.
    JobTraceSession job_trace(job);

    if (job.mode == RenderMode::Video) {
        if (!job.compiled || !job.compiled->composition) {
            return make_render_job_error(
                RenderJobErrorCode::InvalidJob,
                "Video RenderJob requires the legacy compiled export view");
        }
        if (!job.selected_frames.empty()) {
            return make_render_job_error(
                RenderJobErrorCode::InvalidJob,
                "InvalidRange: video RenderJob cannot use "
                "non-contiguous selected_frames");
        }
#ifdef CHRONON3D_HAS_CLI_VIDEO_EXPORT
        if (!validate_video_job(job)) {
            return make_render_job_error(
                RenderJobErrorCode::ValidationFailed,
                "EncoderFailed: video export validation failed for composition '" +
                    job.comp_id + "' using codec '" +
                    job.video_settings.codec + "' output '" +
                    job.output + "'");
        }

        int rc = 0;
        if (job.video_settings.dry_run) {
            rc = dry_run_video_job(job);
        } else {
            auto opts = make_ffmpeg_export_options(job);
            chronon3d::CancellationToken cancel_token;
            install_signal_cancellation(cancel_token);
            opts.cancellation_token = &cancel_token;

            // The video exporter runs the render loop in-process.  A backend
            // that cannot execute the node contract (e.g. Vulkan before
            // RenderSurface execution is wired) throws from the render loop;
            // convert that into a structured RenderJobError instead of letting
            // the exception terminate the CLI (core dump).
            try {
                rc = render_and_encode_ffmpeg(
                    *job.registry,
                    *job.compiled,
                    job.comp_id,
                    job.settings,
                    job.first_frame,
                    job.last_frame + Frame{1},
                    opts,
                    job.execution.cpu_budget,
                    std::move(execution));
            } catch (const std::exception& error) {
                return make_render_job_error(
                    RenderJobErrorCode::RenderFailed,
                    "BackendFailed: video export for composition '" +
                        job.comp_id + "' threw: " + error.what());
            } catch (...) {
                return make_render_job_error(
                    RenderJobErrorCode::RenderFailed,
                    "BackendFailed: video export for composition '" +
                        job.comp_id + "' threw an unknown exception");
            }
        }

        if (rc != 0) {
            return make_render_job_error(
                RenderJobErrorCode::RenderFailed,
                "EncoderFailed: video encoder failed for composition '" +
                    job.comp_id + "' using codec '" +
                    job.video_settings.codec + "' output '" +
                    job.output + "'");
        }

        write_render_job_receipt(job);
        const int frames = static_cast<int>(
            job.last_frame.integral() - job.first_frame.integral() + 1);
        return RenderJobOutput{
            .mode = RenderMode::Video,
            .output = job.output,
            .frames_written = job.video_settings.dry_run ? 0 : frames,
        };
#else
        return make_render_job_error(
            RenderJobErrorCode::UnsupportedMode,
            "Video output requested but the CLI video exporter target is disabled");
#endif
    }

    try {
        RenderJobSetupResult setup;
        setup_render_job(*job.registry, job, setup, std::move(warm_renderer));
        if (!setup.renderer) {
            return make_render_job_error(
                RenderJobErrorCode::SetupFailed,
                "Failed to create renderer for composition '" +
                    job.comp_id + "'");
        }
        if (!setup.preparation_ok) {
            return make_render_job_error(
                RenderJobErrorCode::SetupFailed,
                "Render preparation failed for composition '" +
                    job.comp_id + "': " + setup.preparation_diagnostic);
        }

        const auto motion_blur_suffix =
            chronon3d::is_motion_blur_active(job.settings.motion_blur)
                ? fmt::format(" [MB {}smp {:.0f}°/{:.0f}°]",
                              job.settings.motion_blur.samples,
                              job.settings.motion_blur.shutter_angle_deg,
                              job.settings.motion_blur.shutter_phase_deg)
                : std::string{};
        const auto ssaa_suffix = job.settings.ssaa_factor > 1.0f
            ? fmt::format(" [SSAA {:.1f}x]", job.settings.ssaa_factor)
            : std::string{};

        if (!job.selected_frames.empty()) {
            spdlog::info("Rendering {} [{} selected frames]{}{}...",
                         job.comp_id,
                         job.selected_frames.size(),
                         motion_blur_suffix,
                         ssaa_suffix);
        } else {
            const Frame start = job.mode == RenderMode::Still
                ? job.still_frame
                : job.first_frame;
            const Frame end = job.mode == RenderMode::Still
                ? job.still_frame
                : job.last_frame;
            const Frame step = Frame{std::max<std::int64_t>(
                1, job.frame_step.integral())};

            spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
                         job.comp_id,
                         start,
                         end,
                         step,
                         motion_blur_suffix,
                         ssaa_suffix);
        }

        setup.sys_metrics.sample_cpu_start();
        auto loop = run_render_job_loop(job, *setup.renderer);

        const bool ok = finalize_render_job(
            job,
            setup,
            loop.telemetry_frames,
            loop.total_render_ms,
            loop.total_encode_ms,
            loop.frames_written,
            loop.ok,
            loop.loop_start,
            loop.loop_end);
        if (!ok) {
            return make_render_job_error(
                RenderJobErrorCode::RenderFailed,
                "Render failed for composition '" + job.comp_id + "'");
        }

        return RenderJobOutput{
            .mode = job.mode,
            .output = job.output,
            .frames_written = loop.frames_written,
        };
    } catch (const std::exception& error) {
        return make_render_job_error(
            RenderJobErrorCode::RenderFailed,
            error.what());
    }
}

} // namespace chronon3d::cli
