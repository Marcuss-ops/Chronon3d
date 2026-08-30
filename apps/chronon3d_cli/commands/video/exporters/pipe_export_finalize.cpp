#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "../../../utils/telemetry/telemetry_run.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/media/video/output_contract.hpp>

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#endif

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>

namespace chronon3d::cli {

EncoderCloseResult close_pipe_encoder(PipeExportSession& session) {
    EncoderCloseResult result;

    const bool is_native = (session.opts.encoder.encoder_backend == "native");
    result.write_blocked_ms = pipe_write_blocked_ms(is_native, *session.encoder);

    const double conv_copy_ms = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? static_cast<double>(session.renderer_ptr()->counters()->frame_conversion_copy_wall_ms.load())
        : 0.0;
    const auto conversion_bytes = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? session.renderer_ptr()->counters()->conversion_bytes_written.load()
        : 0ULL;
    const auto staging_copy_bytes = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? session.renderer_ptr()->counters()->encoder_staging_copy_bytes.load()
        : 0ULL;
    const auto encoder_slot_reuses = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? session.renderer_ptr()->counters()->encoder_slot_reuses.load()
        : 0ULL;

    spdlog::info("[video] Encoder write blocked duration: {:.2f} ms", result.write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", conv_copy_ms);
    spdlog::info(
        "[video_diag] conversion_bytes_written={} encoder_staging_copy_bytes={} encoder_slot_reuses={}",
        conversion_bytes, staging_copy_bytes, encoder_slot_reuses);

    result.success = session.encoder->close();
    if (!result.success) {
        spdlog::error("[video] Encoder close failed");
    }

    // Read the native accessors AFTER close() so the flush-time
    // receive/mux/trailer accumulation (drain + av_write_trailer) is
    // included.  Reading before close() would silently drop the final
    // packets and the trailer, under-reporting those tails.
    result.native_convert_ms      = session.encoder->native_convert_ms();
    result.native_send_ms         = session.encoder->native_send_frame_ms();
    result.native_backpressure_ms = session.encoder->native_backpressure_ms();
    result.native_cuda_pending_peak = session.encoder->native_cuda_pending_peak();
    result.native_cuda_backpressure_wait_count =
        session.encoder->native_cuda_backpressure_wait_count();
    result.native_flush_ms        = session.encoder->native_flush_ms();
    result.native_receive_ms      = session.encoder->native_receive_packet_ms();
    result.native_mux_ms          = session.encoder->native_mux_write_ms();
    result.native_trailer_ms      = session.encoder->native_trailer_ms();
    result.encoder_hwframe_get_buffer_ms = session.encoder->encoder_hwframe_get_buffer_ms();
    result.encoder_surface_acquire_ms    = session.encoder->encoder_surface_acquire_ms();
    result.encoder_nvenc_submit_ms       = session.encoder->encoder_nvenc_submit_ms();
    result.encoder_queue_backpressure_wait_ms = session.encoder->encoder_queue_backpressure_wait_ms();
    result.encoder_packet_drain_ms       = session.encoder->encoder_packet_drain_ms();
    result.direct_yuv_cuda_launch_ms     = session.encoder->direct_yuv_cuda_launch_ms();
    result.direct_yuv_cuda_wait_ms       = session.encoder->direct_yuv_cuda_wait_ms();

    if (is_native) {
        spdlog::info(
            "[video_native] convert={:.2f}ms  send_frame={:.2f}ms  backpressure={:.2f}ms  "
            "flush={:.2f}ms  receive_packet={:.2f}ms  mux_write={:.2f}ms  trailer={:.2f}ms",
            result.native_convert_ms, result.native_send_ms, result.native_backpressure_ms,
            result.native_flush_ms, result.native_receive_ms, result.native_mux_ms,
            result.native_trailer_ms);
    }

    return result;
}

PipeExportResult make_pipe_export_result(
    const PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    double render_ms,
    double encode_ms,
    double wall_time_ms)
{
    PipeExportResult result;
    const auto& status = loop_result.status;

    result.success = status.success;
    result.cancelled = status.cancelled;
    result.render_failed = status.render_failed;
    result.writer_error = status.writer_error;
    result.exception_error = status.exception_error;
    result.encoder_close_failed = !close_result.success;

    // An encoder close failure overrides success: even if all frames were
    // rendered and encoded, a failed close means the output is incomplete
    // or corrupted. The caller relies on return_code for the process exit
    // code, so both success and return_code must reflect this.
    if (result.encoder_close_failed) {
        result.success = false;
    }

    result.frames_rendered = status.frames_rendered;
    result.frames_enqueued = status.frames_enqueued;
    result.frames_encoded = status.frames_encoded;
    result.wall_time_ms = wall_time_ms;
    result.render_ms = render_ms;
    result.encode_ms = encode_ms;
    result.return_code = result.success ? 0 : 1;

    // P1-B: atomic output — FFmpeg wrote to session.opts.output.output
    // (which has .partial suffix).  On success, rename to the original
    // final path.  On failure, clean up the partial file.
    const auto partial_path = std::filesystem::path(session.opts.output.output);
    const auto final_path = std::filesystem::path(session.original_output_path);

    if (!result.success) {
        if (result.encoder_close_failed) {
            spdlog::error("[video] Export failed: encoder close failed after all frames rendered");
        }
        log_pipe_export_failure(status);
        // Clean up partial output on failure
        std::error_code ec;
        if (std::filesystem::exists(partial_path, ec)) {
            std::filesystem::remove(partial_path, ec);
            if (ec) {
                spdlog::warn("[video] Failed to remove partial output {}: {}",
                             partial_path.string(), ec.message());
            }
        }
    } else {
        // P1-B: OutputContract verification before rename.
        // The canonical production contract (h264/yuv420p/24fps) is resolved
        // once; runtime geometry/fps/frame count override it. The pipe export
        // is video-only (audio is muxed by the external mux boundary later),
        // so audio is not required at this stage.
        const auto validation_t0 = profiling::now();
        auto contract_result =
            media::video::resolve_output_contract("youtube_overlay_v1");
        if (!contract_result) {
            spdlog::error("[video] output contract unavailable: {}",
                          std::move(contract_result).error());
            result.success = false;
            result.return_code = 1;
            std::error_code ec;
            std::filesystem::remove(partial_path, ec);
            return result;
        }
        auto contract = std::move(contract_result).value();
        contract.width = session.canvas_width;
        contract.height = session.canvas_height;
        contract.fps = session.opts.output.frame_rate();
        contract.frame_count = session.total_frames;
        contract.audio_required = false;
        contract.audio_streams = 0;

        // The encoder writes the temporary `.partial.mp4` path. Verify that
        // durable bytes before the atomic rename to the final output path;
        // the final path intentionally does not exist yet at this point.
        const auto verification = media::video::verify_output_contract(
            partial_path, contract);
        result.validation_ms = profiling::duration_ms(validation_t0, profiling::now());
        result.sha256 = verification.sha256;
        result.ffprobe_ms = verification.ffprobe_ms;
        result.sha256_ms = verification.sha256_ms;
        result.copy_eligible = verification.copy_eligible;

        if (!verification.passed) {
            spdlog::error("[video] output verification failed — {}",
                          verification.failure);
            result.success = false;
            result.return_code = 1;
            std::error_code ec;
            std::filesystem::remove(partial_path, ec);
            return result;
        }
        if (!verification.copy_eligible) {
            spdlog::warn("[video] artifact decodable but not copy-eligible — {}",
                         verification.failure);
        } else {
            spdlog::info("[video] copy_eligible=true sha256={}", result.sha256);
        }

        // Atomic rename: .partial → final path
        const auto output_t0 = profiling::now();
        std::error_code ec;
        std::filesystem::rename(partial_path, final_path, ec);
        result.output_finalize_ms = profiling::duration_ms(output_t0, profiling::now());
        if (ec) {
            spdlog::error("[video] Failed to rename {} → {}: {}",
                         partial_path.string(), final_path.string(), ec.message());
            result.success = false;
            result.return_code = 1;
            // Clean up partial on rename failure too
            std::filesystem::remove(partial_path, ec);
        } else {
            result.output_published = true;
            spdlog::info("[video] Wrote {}", session.original_output_path);
        }
    }

    return result;
}

void record_pipe_telemetry(
    const std::string& composition_id,
    PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& telemetry_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms,
    const PipeExportResult& result)
{
    const bool is_native = (session.opts.encoder.encoder_backend == "native");
    auto* counters = session.renderer_ptr() ? session.renderer_ptr()->counters()
                                      : &session.direct_yuv_session->counters;
    const double conv_copy_ms = counters
        ? static_cast<double>(counters->frame_conversion_copy_wall_ms.load())
        : 0.0;

    // ── Merge encoder telemetry into frame records ────────────────────────
    std::vector<chronon3d::telemetry::FrameTelemetry> sorted_encoder = session.frame_encoder_telemetry;
    std::sort(sorted_encoder.begin(), sorted_encoder.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    auto mutable_frames = telemetry_frames;
    auto encode_it = sorted_encoder.begin();
    for (auto& frame : mutable_frames) {
        while (encode_it != sorted_encoder.end() && encode_it->frame_number < frame.frame_number) {
            ++encode_it;
        }
        if (encode_it == sorted_encoder.end() || encode_it->frame_number != frame.frame_number) {
            continue;
        }
        frame.conversion_copy_ms = encode_it->conversion_copy_ms;
        frame.encoder_ms = encode_it->encoder_ms;
        frame.pipe_write_ms = encode_it->pipe_write_ms;
        frame.backpressure_wait_ms = encode_it->backpressure_wait_ms;
        frame.pipe_write_cpu_ms = encode_it->pipe_write_cpu_ms;
        frame.pipe_backpressure_wait_ms = encode_it->pipe_backpressure_wait_ms;
        frame.native_convert_ms = encode_it->native_convert_ms;
        frame.native_send_ms = encode_it->native_send_ms;
        frame.native_receive_ms = encode_it->native_receive_ms;
        frame.native_mux_ms = encode_it->native_mux_ms;
    }

    std::sort(mutable_frames.begin(), mutable_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    // ── Collect all telemetry events ───────────────────────────────────────
    // Event stores exist ONLY for the SQLite consumer below; the drain is
    // gated on the same define (TICKET-TELEMETRY-STORE-CONSUMER-AUDIT).
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    auto telemetry = chronon3d::telemetry::collect_all_telemetry();
#endif

    // ── Phase records ──────────────────────────────────────────────────────
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;
    const bool direct_yuv = session.direct_yuv_selected();

    if (!direct_yuv && session.renderer_ptr() && counters) {
        auto graph_phases = cli::telemetry::capture_graph_phase_records(*counters);
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }

    const double queue_wait_ms = loop_result.queue_wait_ms;
    const double writer_encode_ms = static_cast<double>(
        session.writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;

    phases.push_back({"rendering_loop", render_ms});
    phases.push_back({"encoder_close_and_flush", encode_ms});
    if (!direct_yuv) {
        phases.push_back({"chronon_render_pure_ms", loop_result.render_graph_eval_ms});
        phases.push_back({"chronon_render_only_ms", loop_result.render_graph_eval_ms});
    }
    phases.push_back({"chronon_render_loop_ms", render_ms});
    phases.push_back({"chronon_conversion_copy_ms", conv_copy_ms});
    phases.push_back({"chronon_queue_wait_ms", queue_wait_ms});
    phases.push_back({"chronon_writer_encode_ms", writer_encode_ms});

    if (is_native) {
        phases.push_back({"native_av_convert_ms", close_result.native_convert_ms});
        phases.push_back({"native_av_send_frame_ms", close_result.native_send_ms});
        phases.push_back({"native_av_receive_packet_ms", close_result.native_receive_ms});
        phases.push_back({"native_av_mux_write_ms", close_result.native_mux_ms});
        phases.push_back({"native_av_trailer_ms", close_result.native_trailer_ms});
    } else {
        phases.push_back({"ffmpeg_encode_total_ms", close_result.write_blocked_ms});
    }

    // ── Canonical per-phase breakdown (GPU overlay factory view) ────────────
    // scene_eval / gpu_render split the render loop so pixel work is never
    // masked by easing/layout/scheduling.  gpu_readback / encode / disk_io
    // split the writer side so codec and I/O never mask GPU readback.
    const uint64_t node_execute_ms = counters
        ? counters->node_execute_actual_wall_ms.load(std::memory_order_relaxed)
        : 0ULL;

    chronon3d::telemetry::RenderPhaseTimings phase_timings;
    phase_timings.gpu_render_ms = direct_yuv
        ? 0.0 : static_cast<double>(node_execute_ms);
    phase_timings.scene_eval_ms = direct_yuv
        ? 0.0
        : std::max(0.0, loop_result.render_graph_eval_ms - phase_timings.gpu_render_ms);

    double readback_sum = 0.0;
    double codec_sum = 0.0;
    double pipe_sum = 0.0;
    for (const auto& f : session.frame_encoder_telemetry) {
        if (is_native) {
            readback_sum += f.native_convert_ms;
            codec_sum += f.native_send_ms + f.native_receive_ms;
            pipe_sum += f.native_mux_ms;
        } else {
            readback_sum += f.conversion_copy_ms;
            codec_sum += f.encoder_ms;
            pipe_sum += f.pipe_write_ms;
        }
    }
    // Fall back to the already-aggregated totals when the per-frame breakdown
    // is empty (zero-frame / error paths).
    if (session.frame_encoder_telemetry.empty()) {
        readback_sum = conv_copy_ms;
        codec_sum = writer_encode_ms;
        pipe_sum = is_native ? 0.0 : close_result.write_blocked_ms;
    }
    phase_timings.gpu_readback_ms = readback_sum;
    phase_timings.encode_ms = codec_sum;
    // encode_ms is the close+flush tail (render_end → wall end): the final
    // bytes-to-disk portion that the per-frame pipe/mux breakdown excludes.
    phase_timings.disk_io_ms = pipe_sum + encode_ms;

    const auto canonical_phases = phase_timings.to_phase_records();
    phases.insert(phases.end(), canonical_phases.begin(), canonical_phases.end());

    // ── Counters ───────────────────────────────────────────────────────────
    if (counters) {
        session.sys_metrics.fill_system_counters(*counters);

        if (is_native) {
            counters->native_av_convert_wall_ms.store(
                static_cast<uint64_t>(close_result.native_convert_ms), std::memory_order_relaxed);
            counters->native_av_receive_packet_wall_ms.store(
                static_cast<uint64_t>(close_result.native_receive_ms), std::memory_order_relaxed);
            counters->native_av_mux_write_wall_ms.store(
                static_cast<uint64_t>(close_result.native_mux_ms), std::memory_order_relaxed);
        } else {
            counters->video_pipe_write_wall_ms.store(
                static_cast<uint64_t>(close_result.write_blocked_ms), std::memory_order_relaxed);
            counters->ffmpeg_pipe_write_wall_ms.store(
                static_cast<uint64_t>(close_result.write_blocked_ms), std::memory_order_relaxed);
        }
    }

    auto resolved_counters = telemetry::capture_counters(*counters);
    resolved_counters.push_back({"ffmpeg_pipe_write_blocked_duration_ms",
        static_cast<uint64_t>(std::llround(close_result.write_blocked_ms))});
    resolved_counters.push_back({"ffmpeg_queue_wait_duration_ms",
        static_cast<uint64_t>(std::llround(loop_result.queue_wait_ms))});

    if (session.renderer_ptr() && session.renderer_ptr()->framebuffer_pool()) {
        auto pool_stats = session.renderer_ptr()->framebuffer_pool()->stats();
        resolved_counters.push_back({"framebuffer_pool_capacity", pool_stats.max_bytes});
        resolved_counters.push_back({"framebuffer_pool_available_count", pool_stats.available_count});
        resolved_counters.push_back({"framebuffer_pool_current_bytes", pool_stats.current_bytes});
        resolved_counters.push_back({"framebuffer_pool_total_allocations", pool_stats.total_allocations});
        resolved_counters.push_back({"framebuffer_pool_total_reuses", pool_stats.total_reuses});
        const auto configured_pool_budget =
            session.renderer_ptr()->runtime().config().cache().fb_pool_budget_bytes();
        resolved_counters.push_back({
            "framebuffer_pool_budget_bytes",
            configured_pool_budget > 0 ? configured_pool_budget : pool_stats.budget_bytes});
        resolved_counters.push_back({"framebuffer_pool_retained_bytes", pool_stats.retained_bytes});
        resolved_counters.push_back({"framebuffer_pool_evicted_count", pool_stats.evicted_count});
        resolved_counters.push_back({"framebuffer_pool_evicted_bytes", pool_stats.evicted_bytes});
        resolved_counters.push_back({"framebuffer_pool_pressure_count", pool_stats.pressure_count});
        resolved_counters.push_back({"framebuffer_pool_size_class_count", pool_stats.size_class_count});
    }

    // GPU backend counters (vkQueueSubmit count + executed command-plan
    // passes) flow into render_counters so the summary can print them next to
    // the CPU/FFmpeg breakdown.  Software backends contribute nothing.
    if (session.renderer_ptr() && session.renderer_ptr()->runtime().backend_attached()) {
        std::vector<std::pair<std::string, std::uint64_t>> gpu_counters;
        session.renderer_ptr()->runtime().backend().export_gpu_telemetry_counters(gpu_counters);
        for (const auto& [name, value] : gpu_counters) {
            resolved_counters.push_back({name, value});
        }
    }

    // ── Compute render artifact (P0 video/text — Fase 1) ────────────────────
    // `make_pipe_export_result` already ran (it precedes this call), so the
    // artifact is described from the *published* final path and carries the
    // SHA-256 digest instead of an empty placeholder.
    std::vector<chronon3d::telemetry::RenderArtifactRecord> artifacts;
    {
        namespace fs = std::filesystem;
        const std::string out_path = result.output_published
            ? session.original_output_path
            : session.opts.output.output;
        chronon3d::telemetry::RenderArtifactRecord artifact;
        artifact.run_id = "";  // filled by record_output_run
        artifact.type = "video";
        artifact.path = out_path;
        artifact.sha256 = result.sha256;
        std::error_code ec;
        artifact.file_exists = fs::exists(out_path, ec);
        if (artifact.file_exists) {
            artifact.size_bytes = static_cast<int64_t>(fs::file_size(out_path, ec));
            if (ec) artifact.size_bytes = 0;
        }
        artifacts.push_back(artifact);
    }

    // ── Record ─────────────────────────────────────────────────────────────
    const int encoded_frames = pipe_encoded_frame_count(loop_result.status);
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    cli::telemetry::record_output_run(
        composition_id,
        result.output_published ? session.original_output_path : session.opts.output.output,
        result.success,
        static_cast<int>(session.total_frames), encoded_frames,
        wall_time_ms, render_ms, close_result.write_blocked_ms,
        session.started_at_iso, phases, resolved_counters,
        telemetry.node_events, counters, mutable_frames,
        telemetry.layer_events, telemetry.cache_events, telemetry.culling_events,
        telemetry.image_events, artifacts);
#endif
}

} // namespace chronon3d::cli
