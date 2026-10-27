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
#include "../video/media_contract_from_probe.hpp"
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/verification/render_receipt.hpp>
#endif
#ifdef CHRONON3D_HAS_CLI_VIDEO_EXPORT
#include <chronon3d/media/video/media_probe.hpp>
#endif

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_VERIFICATION

void write_render_job_receipt(const RenderJob& job,
                              const media::VideoJobExecutionContext* execution) {
    if (!job.execution.report || job.output.empty() || job.program) return;

    verification::RenderReceiptInput input;
    input.job_id = job.comp_id;
    input.git_sha = chronon3d::telemetry::TelemetryManager::get_git_commit();
    input.backend = job.video_settings.hardware_encoder == "none"
        ? "software" : "vulkan";
    input.render_lane = input.backend;
    input.lane_reason = "selected";
    input.width = job.metadata.width;
    input.height = job.metadata.height;
    input.fps_num = job.video_settings.fps_num;
    input.fps_den = job.video_settings.fps_den;
    input.first_frame = static_cast<std::int64_t>(
        (job.mode == RenderMode::Still ? job.still_frame : job.first_frame).integral());
    input.frames = job.mode == RenderMode::Video
        ? static_cast<std::int64_t>(job.last_frame.integral() -
                                    job.first_frame.integral() + 1)
        : static_cast<std::int64_t>(job.frame_count().integral());
    input.requested_codec = job.video_settings.codec.empty()
        ? "auto" : job.video_settings.codec;
    input.output_color_space = job.video_settings.color_output;
    input.reference_white_nits = job.video_settings.reference_white_nits;
    input.hdr_mastering_metadata = job.video_settings.hdr_mastering_metadata;
    input.has_audio_tracks = false;
    // FIX 2 — one probe, one digest per artifact. Preferred source: the facts
    // the export boundary already computed on the freshly published bytes
    // (`verify_output_contract`, published through the execution context).
    // Missing facts fall back to inspecting the output ONCE here (canonical
    // in-process probe, ~15 ms) and digesting it once. Either way the receipt
    // builder never re-hashes the same bytes nor spawns its own ffprobe
    // (~300 ms). HDR-mastering jobs keep the builder's own probe: the
    // mastering comparison needs side data the in-process probe does not
    // expose, and the builder falls back automatically.
    if (execution != nullptr) {
        const auto& observed = execution->observed_output;
        if (observed.probe.has_value()) {
            input.precomputed_media = media_contract_from_probe(*observed.probe);
            input.precomputed_probe_ms = observed.probe_ms;
        }
        if (!observed.sha256.empty()) {
            input.precomputed_sha256 = observed.sha256;
            input.precomputed_sha256_ms = observed.sha256_ms;
        }
    }
    if (!job.video_settings.output_segments.empty()) {
        for (std::size_t index = 0;
             index < job.video_settings.output_segments.size(); ++index) {
            const auto& segment = job.video_settings.output_segments[index];
            std::error_code path_ec;
            if (!std::filesystem::is_regular_file(segment.output_path, path_ec) || path_ec) {
                spdlog::warn("segment receipt skipped; no verified output was published: {}",
                             segment.output_path);
                continue;
            }
            auto segment_input = input;
            segment_input.first_frame += segment.start_frame;
            segment_input.frames = segment.end_frame - segment.start_frame;
            segment_input.precomputed_media.reset();
            segment_input.precomputed_probe_ms.reset();
            segment_input.precomputed_sha256.reset();
            segment_input.precomputed_sha256_ms.reset();
            if (execution != nullptr) {
                const auto observed_it = std::find_if(
                    execution->observed_segment_outputs.begin(),
                    execution->observed_segment_outputs.end(),
                    [&segment](const auto& observed) {
                        return observed.output_path == segment.output_path &&
                            observed.start_frame == segment.start_frame &&
                            observed.end_frame == segment.end_frame;
                    });
                if (observed_it != execution->observed_segment_outputs.end()) {
                    const auto& observed = *observed_it;
                    if (observed.probe) {
                        segment_input.precomputed_media =
                            media_contract_from_probe(*observed.probe);
                        segment_input.precomputed_probe_ms = observed.probe_ms;
                    }
                    if (!observed.sha256.empty()) {
                        segment_input.precomputed_sha256 = observed.sha256;
                        segment_input.precomputed_sha256_ms = observed.sha256_ms;
                    }
                }
            }
            const auto receipt = verification::build_render_receipt(
                segment_input, segment.output_path, true);
            const auto receipt_path = verification::write_render_receipt(
                receipt, segment.output_path);
            spdlog::info("segment receipt: {} verification={} copy_eligible={}",
                         receipt_path.string(), receipt.verification.resolved_policy,
                         receipt.copy_eligible ? "true" : "false");
        }
        // A requested mirror is an artifact too: its receipt must accompany
        // the per-item receipts so the batch consumer can verify it without
        // re-probing or losing the canonical identity claim.
        std::error_code mirror_ec;
        if (job.output.empty() ||
            !std::filesystem::is_regular_file(job.output, mirror_ec) || mirror_ec) {
            return;
        }
    }
#ifdef CHRONON3D_HAS_CLI_VIDEO_EXPORT
    if (!input.precomputed_media.has_value() && job.mode == RenderMode::Video) {
        const auto probe_t0 = chronon3d::profiling::now();
        if (const auto probe = chronon3d::media::video::probe_media(job.output)) {
            input.precomputed_media = media_contract_from_probe(probe.value());
        }
        input.precomputed_probe_ms =
            chronon3d::profiling::duration_ms(probe_t0, chronon3d::profiling::now());
    }
#endif
    if (!input.precomputed_sha256.has_value()) {
        const auto sha_t0 = chronon3d::profiling::now();
        if (const auto digest = chronon3d::assets::sha256_file(job.output)) {
            input.precomputed_sha256 = digest->hex();
        }
        input.precomputed_sha256_ms =
            chronon3d::profiling::duration_ms(sha_t0, chronon3d::profiling::now());
    }
    const auto build_t0 = chronon3d::profiling::now();
    const auto receipt = verification::build_render_receipt(
        input, job.output,
        job.mode == RenderMode::Video);
    const auto build_t1 = chronon3d::profiling::now();
    const auto receipt_path = verification::write_render_receipt(
        receipt, job.output);
    const auto write_t1 = chronon3d::profiling::now();
    spdlog::info(
        "receipt: {} verification={} copy_eligible={}",
        receipt_path.string(),
        receipt.verification.resolved_policy,
        receipt.copy_eligible ? "true" : "false");
    spdlog::info(
        "[INFO] job-lifecycle: receipt_build_verify_ms={:.1f} receipt_file_write_ms={:.1f}",
        chronon3d::profiling::duration_ms(build_t0, build_t1),
        chronon3d::profiling::duration_ms(build_t1, write_t1));
}
#else
void write_render_job_receipt(const RenderJob&,
                              const media::VideoJobExecutionContext*) {}
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

        // Fix 2 (TICKET-OVERLAY-THROUGHPUT): the receipt step reuses the
        // export boundary's observed probe/digest from the execution context,
        // so keep the context reachable across the exporter's move below.
        const auto observed_context = execution;
        int rc = 0;
        std::chrono::steady_clock::time_point sidecar_written_at{};
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
                    std::move(execution),
                    &sidecar_written_at,
                    std::move(warm_renderer));
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

        write_render_job_receipt(job, observed_context.get());
        const auto receipt_t1 = chronon3d::profiling::now();
        if (sidecar_written_at != std::chrono::steady_clock::time_point{}) {
            spdlog::info(
                "[INFO] job-lifecycle: post_sidecar_to_receipt_ms={:.1f}",
                chronon3d::profiling::duration_ms(sidecar_written_at, receipt_t1));
        }
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
            .render_lane = setup.renderer ? setup.renderer->resolved_lane_name() : "software",
            .lane_reason = "selected",
        };
    } catch (const std::exception& error) {
        return make_render_job_error(
            RenderJobErrorCode::RenderFailed,
            error.what());
    }
}

} // namespace chronon3d::cli
