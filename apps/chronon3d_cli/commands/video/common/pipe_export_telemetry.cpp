#include "pipe_export_telemetry.hpp"
#include "pipe_export_helpers.hpp"
#include "../../../utils/telemetry/telemetry_run.hpp"

#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>

namespace chronon3d::cli {

std::vector<chronon3d::telemetry::PhaseTelemetryRecord>
    build_pipe_export_phases(
        const PipeExportBenchmarkData& bench,
        const RenderCounters* counters,
        bool is_native)
{
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;

    phases.push_back({"setup_renderer", bench.setup_ms});

    if (counters) {
        auto graph_phases = cli::telemetry::capture_graph_phase_records(*counters);
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }

    phases.push_back({"rendering_loop", bench.render_ms});
    phases.push_back({"encoder_close_and_flush", bench.encode_ms});
    phases.push_back({"chronon_render_pure_ms", bench.render_graph_eval_ms});
    phases.push_back({"chronon_render_only_ms", bench.render_graph_eval_ms});
    phases.push_back({"chronon_render_loop_ms", bench.render_ms});
    phases.push_back({"chronon_conversion_copy_ms", bench.conv_copy_ms});
    phases.push_back({"chronon_queue_wait_ms", bench.queue_wait_ms});
    phases.push_back({"chronon_writer_encode_ms", bench.writer_encode_ms});

    if (is_native) {
        phases.push_back({"native_av_convert_ms", bench.native_convert_ms});
        phases.push_back({"native_av_send_frame_ms", bench.native_send_ms});
        phases.push_back({"native_av_receive_packet_ms", bench.native_receive_ms});
        phases.push_back({"native_av_mux_write_ms", bench.native_mux_ms});
        phases.push_back({"native_av_trailer_ms", bench.native_trailer_ms});
    } else {
        phases.push_back({"ffmpeg_encode_total_ms", bench.write_blocked_ms});
        phases.push_back({"ffmpeg_flush_close_ms", bench.encode_ms});
    }
    phases.push_back({"e2e_wall_ms", bench.wall_time_ms});

    return phases;
}

std::vector<chronon3d::telemetry::CounterTelemetryRecord>
    build_pipe_export_counters(
        const RenderCounters& counters,
        SoftwareRenderer* sw_renderer,
        double write_blocked_ms,
        double queue_wait_ms)
{
    auto result = cli::telemetry::capture_counters(counters);

    result.push_back({"ffmpeg_pipe_write_blocked_duration_ms",
                      static_cast<uint64_t>(std::llround(write_blocked_ms))});
    result.push_back({"ffmpeg_queue_wait_duration_ms",
                      static_cast<uint64_t>(std::llround(queue_wait_ms))});

    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        auto pool_stats = sw_renderer->framebuffer_pool()->stats();
        result.push_back({"framebuffer_pool_capacity", pool_stats.max_bytes});
        result.push_back({"framebuffer_pool_available_count", pool_stats.available_count});
        result.push_back({"framebuffer_pool_current_bytes", pool_stats.current_bytes});
        result.push_back({"framebuffer_pool_total_allocations", pool_stats.total_allocations});
        result.push_back({"framebuffer_pool_total_reuses", pool_stats.total_reuses});
    }

    return result;
}

void merge_encoder_telemetry(
    std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    std::vector<FrameEncoderTelemetryRecord>& frame_encoder_telemetry)
{
    std::sort(frame_encoder_telemetry.begin(), frame_encoder_telemetry.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    auto encode_it = frame_encoder_telemetry.begin();
    for (auto& frame : telemetry_frames) {
        while (encode_it != frame_encoder_telemetry.end() &&
               encode_it->frame_number < frame.frame_number) {
            ++encode_it;
        }
        if (encode_it == frame_encoder_telemetry.end() ||
            encode_it->frame_number != frame.frame_number) {
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
}

void record_pipe_export_run(
    const std::string& composition_id,
    const std::string& output_path,
    const PipeExportStatus& status,
    int total_frames,
    const PipeExportBenchmarkData& bench,
    const std::string& started_at_iso,
    const std::vector<chronon3d::telemetry::PhaseTelemetryRecord>& phases,
    const std::vector<chronon3d::telemetry::CounterTelemetryRecord>& counters,
    const chronon3d::telemetry::TelemetryBundle& telemetry,
    const RenderCounters* counters_src)
{
    const int encoded_frames = pipe_encoded_frame_count(status);

    cli::telemetry::record_output_run(
        composition_id, output_path, status.success,
        total_frames, encoded_frames,
        bench.wall_time_ms, bench.render_ms, bench.encode_ms,
        started_at_iso, phases, counters,
        telemetry.node_events, counters_src,
        telemetry.frames,
        telemetry.layer_events, telemetry.cache_events, telemetry.culling_events,
        telemetry.text_events, telemetry.image_events, telemetry.tile_events);
}

} // namespace chronon3d::cli
