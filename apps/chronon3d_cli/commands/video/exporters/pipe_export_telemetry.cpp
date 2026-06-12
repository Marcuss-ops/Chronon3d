#include "../common/pipe_export_session.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "../../../utils/telemetry/telemetry_run.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>

#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace chronon3d::cli {

void record_pipe_telemetry(
    const std::string& composition_id,
    PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms)
{
    const bool is_native = (session.opts.encoder_backend == "native");
    const double conv_copy_ms = session.renderer && session.renderer->counters()
        ? static_cast<double>(session.renderer->counters()->frame_conversion_copy_ms.load())
        : 0.0;

    // ── Merge encoder telemetry into frame records ────────────────────────
    std::vector<FrameEncoderTelemetryRecord> sorted_encoder = session.frame_encoder_telemetry;
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
        frame.native_convert_ms = encode_it->native_convert_ms;
        frame.native_send_ms = encode_it->native_send_ms;
        frame.native_receive_ms = encode_it->native_receive_ms;
        frame.native_mux_ms = encode_it->native_mux_ms;
    }

    std::sort(mutable_frames.begin(), mutable_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    // ── Collect all telemetry events ───────────────────────────────────────
    auto telemetry = chronon3d::telemetry::collect_all_telemetry();

    // ── Phase records ──────────────────────────────────────────────────────
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;

    if (session.renderer->counters()) {
        auto graph_phases = cli::telemetry::capture_graph_phase_records(*session.renderer->counters());
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }

    const double queue_wait_ms = loop_result.queue_wait_ms;
    const double writer_encode_ms = static_cast<double>(
        session.writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;

    phases.push_back({"rendering_loop", render_ms});
    phases.push_back({"encoder_close_and_flush", encode_ms});
    phases.push_back({"chronon_render_pure_ms", loop_result.render_graph_eval_ms});
    phases.push_back({"chronon_render_only_ms", loop_result.render_graph_eval_ms});
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

    // ── Counters ───────────────────────────────────────────────────────────
    if (session.renderer->counters()) {
        session.sys_metrics.fill_system_counters(*session.renderer->counters());

        if (is_native) {
            session.renderer->counters()->native_av_convert_ms.store(
                static_cast<uint64_t>(close_result.native_convert_ms), std::memory_order_relaxed);
            session.renderer->counters()->native_av_send_frame_ms.store(
                static_cast<uint64_t>(close_result.native_send_ms), std::memory_order_relaxed);
            session.renderer->counters()->native_av_receive_packet_ms.store(
                static_cast<uint64_t>(close_result.native_receive_ms), std::memory_order_relaxed);
            session.renderer->counters()->native_av_mux_write_ms.store(
                static_cast<uint64_t>(close_result.native_mux_ms), std::memory_order_relaxed);
            session.renderer->counters()->native_av_trailer_ms.store(
                static_cast<uint64_t>(close_result.native_trailer_ms), std::memory_order_relaxed);
        } else {
            session.renderer->counters()->video_pipe_write_ms.store(
                static_cast<uint64_t>(close_result.write_blocked_ms), std::memory_order_relaxed);
            session.renderer->counters()->ffmpeg_pipe_write_blocked_ms.store(
                static_cast<uint64_t>(close_result.write_blocked_ms), std::memory_order_relaxed);
        }
    }

    auto resolved_counters = telemetry::capture_counters(*session.renderer->counters());
    resolved_counters.push_back({"ffmpeg_pipe_write_blocked_duration_ms",
        static_cast<uint64_t>(std::llround(close_result.write_blocked_ms))});
    resolved_counters.push_back({"ffmpeg_queue_wait_duration_ms",
        static_cast<uint64_t>(std::llround(loop_result.queue_wait_ms))});

    if (session.sw_renderer && session.sw_renderer->framebuffer_pool()) {
        auto pool_stats = session.sw_renderer->framebuffer_pool()->stats();
        resolved_counters.push_back({"framebuffer_pool_capacity", pool_stats.max_bytes});
        resolved_counters.push_back({"framebuffer_pool_available_count", pool_stats.available_count});
        resolved_counters.push_back({"framebuffer_pool_current_bytes", pool_stats.current_bytes});
        resolved_counters.push_back({"framebuffer_pool_total_allocations", pool_stats.total_allocations});
        resolved_counters.push_back({"framebuffer_pool_total_reuses", pool_stats.total_reuses});
        resolved_counters.push_back({"framebuffer_pool_budget_bytes", pool_stats.budget_bytes});
        resolved_counters.push_back({"framebuffer_pool_retained_bytes", pool_stats.retained_bytes});
        resolved_counters.push_back({"framebuffer_pool_evicted_count", pool_stats.evicted_count});
        resolved_counters.push_back({"framebuffer_pool_evicted_bytes", pool_stats.evicted_bytes});
        resolved_counters.push_back({"framebuffer_pool_pressure_count", pool_stats.pressure_count});
        resolved_counters.push_back({"framebuffer_pool_size_class_count", pool_stats.size_class_count});
    }

    // ── Record ─────────────────────────────────────────────────────────────
    const int encoded_frames = pipe_encoded_frame_count(loop_result.status);
    const auto& counters = session.renderer->counters();

    cli::telemetry::record_output_run(
        composition_id, session.opts.output, loop_result.status.success,
        static_cast<int>(session.total_frames), encoded_frames,
        wall_time_ms, render_ms, close_result.write_blocked_ms,
        session.started_at_iso, phases, resolved_counters,
        telemetry.node_events, counters, mutable_frames,
        telemetry.layer_events, telemetry.cache_events, telemetry.culling_events,
        telemetry.text_events, telemetry.image_events, telemetry.tile_events);
}

} // namespace chronon3d::cli
