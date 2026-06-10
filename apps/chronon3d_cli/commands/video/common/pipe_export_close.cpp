#include "pipe_export_close.hpp"
#include "pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

PipeExportBenchmarkData close_pipe_export(
    IVideoEncoder& encoder,
    RenderCounters* counters,
    double render_ms,
    double encode_ms,
    double wall_time_ms,
    double setup_ms,
    double render_graph_eval_ms,
    double queue_wait_ms,
    std::atomic<uint64_t>& writer_encode_us_total,
    const std::string& encoder_backend)
{
    PipeExportBenchmarkData bench;

    // ── Encoder close ───────────────────────────────────────────────────
    const bool is_native = (encoder_backend == "native");

    bench.write_blocked_ms = pipe_write_blocked_ms(is_native, encoder);
    bench.native_convert_ms = encoder.native_convert_ms();
    bench.native_send_ms = encoder.native_send_frame_ms();
    bench.native_receive_ms = encoder.native_receive_packet_ms();
    bench.native_mux_ms = encoder.native_mux_write_ms();
    bench.native_trailer_ms = encoder.native_trailer_ms();
    bench.conv_copy_ms = counters
        ? static_cast<double>(counters->frame_conversion_copy_ms.load())
        : 0.0;

    // ── Benchmark breakdown timings ─────────────────────────────────────
    bench.setup_ms = setup_ms;
    bench.render_ms = render_ms;
    bench.encode_ms = encode_ms;
    bench.wall_time_ms = wall_time_ms;
    bench.render_graph_eval_ms = render_graph_eval_ms;
    bench.queue_wait_ms = queue_wait_ms;
    bench.writer_encode_ms = static_cast<double>(
        writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;

    // ── Log benchmark breakdown ─────────────────────────────────────────
    spdlog::info("[video] Encoder write blocked duration: {:.2f} ms", bench.write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", bench.conv_copy_ms);
    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", bench.queue_wait_ms);

    if (is_native) {
        spdlog::info("[video_native] convert={:.2f}ms  send_frame={:.2f}ms  receive_packet={:.2f}ms  mux_write={:.2f}ms  trailer={:.2f}ms",
                     bench.native_convert_ms, bench.native_send_ms, bench.native_receive_ms,
                     bench.native_mux_ms, bench.native_trailer_ms);
    }

    spdlog::info("[benchmark_chronon] render_pure={:.2f}ms  render_only={:.2f}ms  render_loop={:.2f}ms  "
                 "conv_copy={:.2f}ms  queue_wait={:.2f}ms  writer_encode={:.2f}ms  throughput={:.2f}ms",
                 bench.render_graph_eval_ms, bench.render_graph_eval_ms, bench.render_ms,
                 bench.conv_copy_ms, bench.queue_wait_ms, bench.writer_encode_ms,
                 bench.render_graph_eval_ms + bench.queue_wait_ms);

    if (is_native) {
        spdlog::info("[benchmark_e2e] native_convert={:.2f}ms  native_send={:.2f}ms  "
                     "native_receive={:.2f}ms  native_mux={:.2f}ms  native_trailer={:.2f}ms  wall={:.2f}ms",
                     bench.native_convert_ms, bench.native_send_ms, bench.native_receive_ms,
                     bench.native_mux_ms, bench.native_trailer_ms, bench.wall_time_ms);
    } else {
        spdlog::info("[benchmark_e2e] ffmpeg_encode={:.2f}ms  ffmpeg_flush_close={:.2f}ms  wall={:.2f}ms",
                     bench.write_blocked_ms, bench.encode_ms, bench.wall_time_ms);
    }

    // ── Store timings into atomic counters for telemetry ────────────────
    if (counters) {
        if (is_native) {
            counters->native_av_convert_ms.store(
                static_cast<uint64_t>(bench.native_convert_ms), std::memory_order_relaxed);
            counters->native_av_send_frame_ms.store(
                static_cast<uint64_t>(bench.native_send_ms), std::memory_order_relaxed);
            counters->native_av_receive_packet_ms.store(
                static_cast<uint64_t>(bench.native_receive_ms), std::memory_order_relaxed);
            counters->native_av_mux_write_ms.store(
                static_cast<uint64_t>(bench.native_mux_ms), std::memory_order_relaxed);
            counters->native_av_trailer_ms.store(
                static_cast<uint64_t>(bench.native_trailer_ms), std::memory_order_relaxed);
        } else {
            const uint64_t wb = static_cast<uint64_t>(bench.write_blocked_ms);
            counters->video_pipe_write_ms.store(wb, std::memory_order_relaxed);
            counters->ffmpeg_pipe_write_blocked_ms.store(wb, std::memory_order_relaxed);
            counters->video_ffmpeg_latency_ms.store(wb, std::memory_order_relaxed);
        }
    }

    return bench;
}

} // namespace chronon3d::cli
