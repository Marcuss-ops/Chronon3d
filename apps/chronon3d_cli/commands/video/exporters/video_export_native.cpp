#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "video_export_native_support.hpp"
#include "pipe_timing_sidecar.hpp"
#include "../../../utils/process_start.hpp"
#include "../../../utils/telemetry/nvml_sampler.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/frame_timing_summary.hpp>
#include <chronon3d/effects/effect_catalog_data.hpp>
#include <chronon3d/effects/effect_type.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif

namespace chronon3d::cli {
PipeExportResult render_and_encode_native(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes,
    runtime::DeviceScheduler* device_scheduler,
    std::shared_ptr<media::VideoJobExecutionContext> execution,
    std::shared_ptr<SoftwareRenderer> warm_renderer)
{
    const auto wall_t0 = profiling::now();
    const auto setup_t0 = profiling::now();

    runtime::DeviceScheduler local_scheduler;
    if (!device_scheduler) {
        if (!configure_local_device_scheduler(opts, local_scheduler, device_scheduler)) {
            return PipeExportResult{};
        }
    }

    auto session = setup_pipe_export_session(
        registry, compiled, settings, opts, start, end, cpu_budget,
        std::move(video_runtimes), device_scheduler, std::move(execution),
        std::move(warm_renderer));
    if (!session || !session->encoder ||
        (!session->renderer_ptr() && !session->direct_yuv_selected()) ||
        (session->direct_yuv_session && session->direct_yuv_session->required_but_unavailable)) {
        return PipeExportResult{};
    }

    if (opts.sink.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    const auto warmup_t0 = profiling::now();
    RenderSettings render_opts = settings;
    runtime::RenderPreparationTimings warmup_prepare_timings;
    if (!session->direct_yuv_selected()) {
        const auto warmup_result = warmup_pipe_renderer(
            *session->renderer_ptr(), compiled, opts, &warmup_prepare_timings);
        if (!warmup_result) {
            spdlog::error("[video] export aborted: render preparation failed: {}",
                          warmup_result.error().message);
            return PipeExportResult{};
        }
        session->prepare_timings.accumulate(warmup_prepare_timings);
        if (!session->renderer_ptr()->backend().supports_native_surfaces()) {
            warmup_pipe_pool(*session);
        }
        warmup_pipe_slots(*session);
        session->full_graph_session->execution_slots.reset_job_state();
    } else {
        spdlog::info("[direct-yuv] bypassed generic warmup_pipe_renderer and 273MB warmup_pipe_pool");
    }
    const auto warmup_t1 = profiling::now();
    if (session->device_runtime) {
        session->cuda_host_upload_bytes_at_steady_start =
            session->device_runtime->cuda_host_upload_bytes();
        session->cuda_host_upload_steady_window_started = true;
    }
    session->prepare_breakdown.pool_warmup_ms = session->direct_yuv_selected()
        ? 0.0 : profiling::duration_ms(warmup_t0, warmup_t1);
    session->sys_metrics.sample_cpu_start();

    telemetry::NvmlSampler nvml_sampler;
    nvml_sampler.start(std::chrono::milliseconds(250));

    auto loop_output = run_pipe_export_loop(*session, registry, compiled, render_opts, start, end, opts);
    auto close_result = close_pipe_encoder(*session);

    media::NativeVideoFrameDecoder::DecodeProfilingStats decode_stats{};
    if (session->native_decoder) {
        decode_stats = session->native_decoder->decode_profiling_stats();
    }
    if (is_native_encoder_transport(session->opts.encoder.encoder_backend) &&
        session->renderer_ptr()) {
        auto& backend = *session->renderer_ptr()->execution_context().backend;
#ifdef CHRONON3D_ENABLE_VULKAN
        if (auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend)) {
            (void)vulkan->wait_for_pending_submissions();
            // Fence completion is the first point at which Vulkan timestamp
            // queries are readable. The backend retains the originating frame
            // index in the ring slot, so apply exact device timing to that
            // frame instead of attributing it to whichever frame performed
            // the drain.
            for (const auto& gpu : vulkan->consume_completed_frame_timings()) {
                const auto it = std::find_if(
                    loop_output.telemetry_frames.begin(),
                    loop_output.telemetry_frames.end(),
                    [&gpu](const auto& frame) {
                        return frame.frame_number ==
                            static_cast<int>(gpu.frame_index);
                    });
                if (it == loop_output.telemetry_frames.end()) {
                    spdlog::warn(
                        "[vulkan] GPU timing completion has no telemetry frame: "
                        "frame={} slot={}",
                        gpu.frame_index, gpu.frame_batch_slot);
                    continue;
                }
                it->gpu_timing.timestamp_attributed = gpu.timestamp_valid;
                it->gpu_timing.frame_batch_slot = gpu.frame_batch_slot;
                it->gpu_timing.timestamp_execute_ms = gpu.execute_ms;
                it->gpu_timing.device_span_ms = gpu.device_span_ms;
                it->gpu_timing.queue_wait_ms = gpu.queue_wait_ms;
                it->gpu_timing.device_start_trace_ns =
                    gpu.device_start_trace_ns;
                it->gpu_timing.device_end_trace_ns =
                    gpu.device_end_trace_ns;
            }
        }
#endif
        session->native_decoder.reset();
        backend.release_frame_transient_surfaces();
    }
    nvml_sampler.stop();
    const auto nvml_stats = nvml_sampler.stats();

    const auto wall_t1 = profiling::now();
    const double wall_time_ms = profiling::duration_ms(wall_t0, wall_t1);
    const double encode_ms = profiling::duration_ms(loop_output.render_end, wall_t1);
    const double writer_encode_ms = static_cast<double>(
        session->writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;

    // nvml_stop_ms is reported next to the other export phases because it used
    // to be attributed to nobody: the sampler is a per-job local whose worker is
    // joined on the serialized job path, so its tail is part of this job's
    // service time. A large value is a driver call in flight (NVML rate-limits
    // the utilization family), never a sleep — the interval wait is
    // interruptible (see utils/telemetry/nvml_sampler.hpp).
    spdlog::info("[video] render_ms={:.2f} encode_ms={:.2f} writer_encode_ms={:.2f} wall_ms={:.2f} nvml_stop_ms={:.2f}",
                 loop_output.render_ms, encode_ms, writer_encode_ms, wall_time_ms,
                 nvml_stats.sampler_stop_ms);
    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", loop_output.loop_result.queue_wait_ms);

    auto result = make_pipe_export_result(*session, loop_output.loop_result, close_result,
                                          loop_output.render_ms, encode_ms, wall_time_ms);
    auto* counters = session->renderer_ptr()
        ? session->renderer_ptr()->counters() : &session->direct_yuv_session->counters;

    // Wave 1D — events only.  Telemetry persistence (record_pipe_telemetry)
    // is removed: Chronon owns no database; hosts/collectors observe
    // Chronon out-of-band and RenderingGen receives only the bounded result.
    const bool is_native =
        is_native_encoder_transport(session->opts.encoder.encoder_backend);
    pipe_timing::JobTimings timings;
    if (session->direct_yuv_selected()) {
        timings.execution_path = "direct_yuv";
    } else if (is_native) {
        timings.execution_path = "full_graph_native";
    } else {
        timings.execution_path = "full_graph";
    }
    if (!session->opts.resolved_execution_reason.empty()) {
        timings.execution_reason = session->opts.resolved_execution_reason;
    }
    timings.execution_downgrade_count = session->opts.resolved_execution_downgrade_count;
    if (session->execution_plan) {
        switch (session->execution_plan->handoff) {
        case media::SurfaceHandoffPath::None:
            timings.surface_handoff_path = "none";
            break;
        case media::SurfaceHandoffPath::Direct:
            timings.surface_handoff_path = "direct";
            break;
        case media::SurfaceHandoffPath::VulkanCopy:
            timings.surface_handoff_path = "vulkan_copy";
            break;
        case media::SurfaceHandoffPath::HostUpload:
            timings.surface_handoff_path = "host_upload";
            break;
        }
    }
    if (counters) {
        if (session->direct_yuv_selected()) {
            counters->nvenc_frames.fetch_add(0, std::memory_order_relaxed);
        }
    }
    timings.job_wall_ms = wall_time_ms;
    // Job-relative, never process-relative: for a single-shot CLI render the
    // session anchor IS the process start, so CLI semantics are unchanged;
    // for a persistent daemon it is the RENDER_JOB arrival, so the recorded
    // pre-job gap can never be daemon uptime.
    timings.process_wall_ms = profiling::duration_ms(session->job_started_at, wall_t0);
    timings.daemon_admission_wait_ms = session->daemon_admission_wait_ms;
    timings.measurement_kind = chronon3d::cli::startup_measurement_kind_name(
        session->warm_process
            ? chronon3d::cli::StartupMeasurementKind::WarmProcess
            : chronon3d::cli::startup_trace().measurement_kind);
    timings.prepare_ms = profiling::duration_ms(setup_t0, warmup_t1);
    timings.engine_init_ms = session->engine_init_ms;
    timings.backend_init_ms = session->backend_init_ms;
    timings.render_loop_wall_ms = loop_output.render_ms;
    timings.encoder_finalize_ms = encode_ms;
    const auto pipeline_samples = session->pipeline_metrics.queue_depth_samples.load(
        std::memory_order_relaxed);
    timings.pipeline.queue_depth_peak = session->pipeline_metrics.queue_depth_peak.load(
        std::memory_order_relaxed);
    timings.pipeline.queue_depth_average = pipeline_samples > 0
        ? std::optional<double>(static_cast<double>(
              session->pipeline_metrics.queue_depth_sum.load(std::memory_order_relaxed)) /
          static_cast<double>(pipeline_samples))
        : std::nullopt;
    timings.pipeline.render_slots_in_flight_peak =
        session->pipeline_metrics.render_slots_in_flight_peak.load(
            std::memory_order_relaxed);
    timings.pipeline.encode_frames_in_flight_peak =
        session->pipeline_metrics.encode_frames_in_flight_peak.load(
            std::memory_order_relaxed);
    if (nvml_stats.sample_count > 0) {
        timings.hardware.gpu_utilization_avg = nvml_stats.gpu_utilization_avg;
        timings.hardware.gpu_utilization_peak = nvml_stats.gpu_utilization_peak;
        timings.hardware.nvdec_utilization_avg = nvml_stats.nvdec_utilization_avg;
        timings.hardware.nvdec_utilization_peak = nvml_stats.nvdec_utilization_peak;
        timings.hardware.nvenc_utilization_avg = nvml_stats.nvenc_utilization_avg;
        timings.hardware.nvenc_utilization_peak = nvml_stats.nvenc_utilization_peak;
        timings.hardware.memory_utilization_avg = nvml_stats.memory_utilization_avg;
        timings.hardware.vram_used_peak_mb = nvml_stats.vram_used_peak_mb;
        timings.hardware.vram_total_mb = nvml_stats.vram_total_mb;
    }
    if (is_native) timings.mux_finalize_ms = close_result.native_trailer_ms;
    if (is_native) {
        timings.encoder.submit_cpu_ms = close_result.native_send_ms;
        timings.encoder.backpressure_wait_ms = close_result.native_backpressure_ms;
        timings.encoder.cuda_pending_peak = close_result.native_cuda_pending_peak;
        timings.encoder.cuda_backpressure_wait_count =
            close_result.native_cuda_backpressure_wait_count;
        timings.encoder.flush_ms = close_result.native_flush_ms;
        timings.encoder.packet_receive_ms = close_result.native_receive_ms;
        timings.encoder.mux_packet_ms = close_result.native_mux_ms;
    } else {
        if (counters) {
            auto* c = counters;
            timings.encoder.pipe_write_cpu_ms = static_cast<double>(
                c->pipe_write_cpu_wall_us.load(std::memory_order_relaxed)) / 1000.0;
            timings.encoder.pipe_backpressure_wait_ms = static_cast<double>(
                c->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        }
    }
    if (!close_result.applied_encoder_preset.empty()) {
        timings.encoder_settings.preset = close_result.applied_encoder_preset;
    }
    if (!close_result.applied_encoder_rate_control.empty()) {
        timings.encoder_settings.rate_control = close_result.applied_encoder_rate_control;
    }
    if (close_result.applied_encoder_async_depth > 0) {
        timings.encoder_settings.async_depth = close_result.applied_encoder_async_depth;
    }
    // Requested (pre-resolution) encoder settings; only populated on the
    // native path where the encoder can report them.
    if (!close_result.requested_encoder_rate_control.empty()) {
        timings.encoder_settings.requested_rate_control =
            close_result.requested_encoder_rate_control;
    }
    if (!close_result.requested_encoder_preset.empty()) {
        timings.encoder_settings.requested_preset =
            close_result.requested_encoder_preset;
    }
    timings.output_finalize_ms = result.output_finalize_ms;
    timings.validation_ms = result.validation_ms;
    timings.ffprobe_ms = result.ffprobe_ms;
    timings.sha256_ms = result.sha256_ms;
    timings.full_file_reread_count = close_result.full_file_reread_count;
    timings.hash.output_write_bytes = close_result.hash_metrics.output_write_bytes;
    timings.hash.output_seek_count = close_result.hash_metrics.output_seek_count;
    timings.hash.output_overwrite_bytes = close_result.hash_metrics.output_overwrite_bytes;
    timings.hash.incremental_hash_invalidations =
        close_result.hash_metrics.incremental_hash_invalidations;
    timings.hash.final_reread_bytes = close_result.hash_metrics.final_reread_bytes;
    timings.target_fps = session->opts.output.fps_value();
    timings.target_fps_num = session->opts.output.frame_rate().numerator;
    timings.target_fps_den = session->opts.output.frame_rate().denominator;
    timings.prepare = session->prepare_timings;
    timings.plan_compile_ms = session->prepare_timings.plan_compile_ms;
    timings.graph_compile_ms = session->prepare_timings.graph_compile_ms;
    // Phase 4 — projection of the render/session counters.
    project_render_counters(*session, counters, timings);

    // Phase 5 — project the backend's GPU telemetry counters.
    project_backend_gpu_counters(*session, timings);
    if (session->renderer_ptr()) {
        const auto& usage = session->renderer_ptr()->session().effect_usage();
        for (std::size_t index = 1; index < usage.by_type.size(); ++index) {
            const auto native = usage.by_type[index].native_frames.load(
                std::memory_order_relaxed);
            const auto rejected = usage.by_type[index].admission_rejected_frames.load(
                std::memory_order_relaxed);
            const auto software = usage.by_type[index].software_dispatches.load(
                std::memory_order_relaxed);
            const auto cpu_time_ns = usage.by_type[index].software_cpu_time_ns.load(
                std::memory_order_relaxed);
            const auto fallback = usage.by_type[index].fallback_count.load(
                std::memory_order_relaxed);
            const auto fallback_cpu_time_ns =
                usage.by_type[index].fallback_cpu_time_ns.load(
                    std::memory_order_relaxed);
            if (native == 0 && rejected == 0 && software == 0 && fallback == 0) continue;
            const auto effect = static_cast<effects::EffectType>(index);
            const auto& entry = effects::catalog_entry(effect);
            timings.effect_usage[std::string(entry.id)] = {
                native, rejected, software, cpu_time_ns, fallback,
                fallback_cpu_time_ns};
        }
    }

    // Phase 6 — job-lifecycle accounting (exclusive wall + breakdowns).
    account_exclusive_wall_timeline(
        *session, loop_output, close_result, result, encode_ms,
        profiling::duration_ms(warmup_t0, warmup_t1), is_native, timings);

    // Phase 7 — internal profiling projection.
    account_internal_profiling(*session, close_result, decode_stats, counters, timings);

    // Job-lifecycle attribution. Measure sidecar serialization separately from
    // releasing the per-output session. The caller carries this timestamp
    // across the exporter boundary and measures the remaining interval through
    // receipt build/verification/write.
    const auto sidecar_t0 = profiling::now();
    write_frame_timing_sidecar(session->original_output_path,
                               loop_output.telemetry_frames,
                               session->frame_encoder_telemetry,
                               wall_time_ms, loop_output.render_ms, encode_ms,
                               timings, is_native);
    const auto sidecar_t1 = profiling::now();
    result.sidecar_written_at = sidecar_t1;
    session.reset();
    const auto session_teardown_t1 = profiling::now();
    spdlog::info(
        "[INFO] job-lifecycle: sidecar_write_ms={:.1f} session_teardown_ms={:.1f}",
        profiling::duration_ms(sidecar_t0, sidecar_t1),
        profiling::duration_ms(sidecar_t1, session_teardown_t1));

    return result;
}

} // namespace chronon3d::cli
