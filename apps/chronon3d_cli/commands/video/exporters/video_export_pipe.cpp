#include "../common/pipe_export_session.hpp"
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <memory>
#include <vector>
#include <algorithm>

namespace chronon3d::cli {

PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    PipeExportResult result;
    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    const int total = static_cast<int>(end - start);
    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();
    const auto setup_t0 = wall_t0;
    std::atomic<uint64_t> writer_encode_us_total{0};
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    std::vector<FrameEncoderTelemetryRecord> frame_encoder_telemetry;
    telemetry_frames.reserve(total > 0 ? static_cast<size_t>(total) : 0);
    frame_encoder_telemetry.reserve(total > 0 ? static_cast<size_t>(total) : 0);

    if (opts.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    const bool codec_auto = opts.codec == "auto";
    const std::string codec = codec_auto ? "libx264" : resolve_cli_ffmpeg_codec(opts.codec, opts.hardware_encoder);

    auto encoder = create_video_encoder(opts);
    chronon3d::SystemMetricsCollector sys_metrics;
    FfmpegPipeOptions pipe_options = make_pipe_options(comp, opts, codec);

    if (!ensure_output_directory_exists(opts.output)) {
        result.return_code = 1;
        return result;
    }

    if (!encoder->open(pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        result.return_code = 1;
        return result;
    }

    track_pipe_encoder_process(opts, *encoder, sys_metrics);

    const auto renderer_t0 = std::chrono::steady_clock::now();
    auto renderer = create_renderer(registry, settings);
    const auto renderer_t1 = std::chrono::steady_clock::now();
    if (renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(renderer_t1 - renderer_t0).count());
        renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());

    cache::NodeCache node_cache;
    video::VideoFrameDecoder* video_decoder = nullptr;

    const size_t arena_size = compute_pipe_arena_size(comp.width(), comp.height());
    TripleBufferArena triple_arena(8, arena_size);

    // ── Shared state between writer thread and render loop ──
    moodycamel::ConcurrentQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<bool> writer_done{false};

    // ── Writer thread ───────────────────────────────────────────────────────
    WriterThreadContext writer_ctx{
        .queue = queue,
        .writer_failed = writer_failed,
        .writer_done = writer_done,
        .triple_arena = triple_arena,
        .encoder = *encoder,
        .renderer = *sw_renderer,
        .writer_encode_us_total = writer_encode_us_total,
        .frame_encoder_telemetry = frame_encoder_telemetry,
    };
    std::thread writer_thread(run_writer_thread, std::ref(writer_ctx));

    // ── Render loop ─────────────────────────────────────────────────────────
    RenderSettings render_opts = settings;
    render_opts.use_modular_graph = true;

    warmup_pipe_renderer(*renderer, comp, opts);

    // ── Pre-warm the framebuffer pool with canvas-sized buffers ────────
    // The async pipeline (render thread + queue + writer thread) can hold
    // up to 4-5 canvas buffers simultaneously.  Pre-allocating 6 ensures
    // the ClearNode COW detach always finds a ready buffer instead of
    // paying for a fresh allocation on every frame (which was causing
    // 119 pool size-mismatch misses in the 300-frame benchmark).
    //
    // This directly targets the `framebuffer_pool_miss_count_size_mismatch`
    // counter.  When the pool has warm buffers, acquire_owned_fb() returns
    // immediately without the ~0.5ms allocation stall.
    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(
            comp.width(), comp.height());
        const auto prealloced = sw_renderer->framebuffer_pool()->preallocate(
            cache::FramebufferPoolPreallocOptions{
                .width = bw,
                .height = bh,
                .count = 6,
                .clear = true,
                .touch_memory = false,
            });
        if (prealloced > 0) {
            spdlog::info("[pool-warm] Pre-allocated {} canvas buffers ({}x{} bucket) at startup",
                         prealloced, bw, bh);
        }
    }

    // Capture CPU baseline before the render phase — fill_system_counters()
    // uses sample_cpu_delta() to compute per-run CPU time.
    sys_metrics.sample_cpu_start();

    const auto render_t0 = std::chrono::steady_clock::now();

    RenderLoopContext loop_ctx{
        .backend = *renderer,
        .node_cache = node_cache,
        .settings = render_opts,
        .registry = registry,
        .video_decoder = video_decoder,
        .comp = comp,
        .start = start,
        .end = end,
        .opts = opts,
        .sw_renderer = sw_renderer,
        .queue = queue,
        .writer_failed = writer_failed,
        .triple_arena = triple_arena,
        .counters = renderer->counters(),
        .telemetry_frames = telemetry_frames,
    };
    auto loop_result = run_render_loop(loop_ctx);

    const auto render_t1 = std::chrono::steady_clock::now();
    writer_done.store(true);
    if (writer_thread.joinable()) {
        writer_thread.join();
    }

    PipeExportStatus& export_status = loop_result.status;
    if (writer_failed.load()) {
        export_status.success = false;
        export_status.writer_error = true;
    }

    // ── Encoder close ───────────────────────────────────────────────────────
    const auto setup_t1 = render_t0;
    const double write_blocked_ms = pipe_write_blocked_ms(opts.encoder_backend == "native", *encoder);

    const double native_convert_ms   = encoder->native_convert_ms();
    const double native_send_ms      = encoder->native_send_frame_ms();
    const double native_receive_ms   = encoder->native_receive_packet_ms();
    const double native_mux_ms       = encoder->native_mux_write_ms();
    const double native_trailer_ms   = encoder->native_trailer_ms();

    const double conv_copy_ms = static_cast<double>(renderer->counters()->frame_conversion_copy_ms.load());
    spdlog::info("[video] Encoder write blocked duration: {:.2f} ms", write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", conv_copy_ms);
    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", loop_result.queue_wait_ms);
    if (opts.encoder_backend == "native") {
        spdlog::info("[video_native] convert={:.2f}ms  send_frame={:.2f}ms  receive_packet={:.2f}ms  mux_write={:.2f}ms  trailer={:.2f}ms",
                     native_convert_ms, native_send_ms, native_receive_ms, native_mux_ms, native_trailer_ms);
    }

    bool encoder_closed = encoder->close();
    if (!encoder_closed) {
        spdlog::error("[video] Encoder close failed");
        export_status.success = false;
        result.encoder_close_failed = true;
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    const double render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();

    // ── Benchmark breakdown ─────────────────────────────────────────────────
    const bool is_native = (opts.encoder_backend == "native");
    const double writer_encode_ms = static_cast<double>(writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;
    const double chronon_render_pure_ms = loop_result.render_graph_eval_ms;
    const double chronon_render_only_ms = loop_result.render_graph_eval_ms;
    const double chronon_render_loop_ms = render_ms;
    const double chronon_queue_wait_ms = loop_result.queue_wait_ms;
    const double chronon_writer_encode_ms = writer_encode_ms;
    const double chronon_conversion_copy_ms = conv_copy_ms;
    const double ffmpeg_encode_total_ms = write_blocked_ms;
    const double ffmpeg_flush_close_ms = encode_ms;

    if (renderer->counters()) {
        if (is_native) {
            renderer->counters()->native_av_convert_ms.store(static_cast<uint64_t>(native_convert_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_send_frame_ms.store(static_cast<uint64_t>(native_send_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_receive_packet_ms.store(static_cast<uint64_t>(native_receive_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_mux_write_ms.store(static_cast<uint64_t>(native_mux_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_trailer_ms.store(static_cast<uint64_t>(native_trailer_ms), std::memory_order_relaxed);
        } else {
            renderer->counters()->video_pipe_write_ms.store(static_cast<uint64_t>(write_blocked_ms), std::memory_order_relaxed);
            renderer->counters()->ffmpeg_pipe_write_blocked_ms.store(static_cast<uint64_t>(write_blocked_ms), std::memory_order_relaxed);
            renderer->counters()->ffmpeg_flush_ms.store(static_cast<uint64_t>(ffmpeg_flush_close_ms), std::memory_order_relaxed);
            renderer->counters()->video_ffmpeg_latency_ms.store(static_cast<uint64_t>(write_blocked_ms), std::memory_order_relaxed);
        }
    }

    spdlog::info("[benchmark_chronon] render_pure={:.2f}ms  render_only={:.2f}ms  render_loop={:.2f}ms  conv_copy={:.2f}ms  queue_wait={:.2f}ms  writer_encode={:.2f}ms  throughput={:.2f}ms",
                 chronon_render_pure_ms, chronon_render_only_ms, chronon_render_loop_ms,
                 chronon_conversion_copy_ms, chronon_queue_wait_ms, chronon_writer_encode_ms,
                 chronon_render_pure_ms + chronon_queue_wait_ms);

    if (is_native) {
        spdlog::info("[benchmark_e2e] native_convert={:.2f}ms  native_send={:.2f}ms  native_receive={:.2f}ms  native_mux={:.2f}ms  native_trailer={:.2f}ms  wall={:.2f}ms",
                     native_convert_ms, native_send_ms, native_receive_ms, native_mux_ms, native_trailer_ms, wall_time_ms);
    } else {
        spdlog::info("[benchmark_e2e] ffmpeg_encode={:.2f}ms  ffmpeg_flush_close={:.2f}ms  wall={:.2f}ms",
                     ffmpeg_encode_total_ms, ffmpeg_flush_close_ms, wall_time_ms);
    }

    // ── Telemetry ───────────────────────────────────────────────────────────
    std::sort(frame_encoder_telemetry.begin(), frame_encoder_telemetry.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    auto encode_it = frame_encoder_telemetry.begin();
    for (auto& frame : telemetry_frames) {
        while (encode_it != frame_encoder_telemetry.end() && encode_it->frame_number < frame.frame_number) {
            ++encode_it;
        }
        if (encode_it == frame_encoder_telemetry.end() || encode_it->frame_number != frame.frame_number) {
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

    std::sort(telemetry_frames.begin(), telemetry_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    auto telemetry = chronon3d::telemetry::collect_all_telemetry();

    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;
    phases.push_back({"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()});
    if (renderer->counters()) {
        auto graph_phases = cli::telemetry::capture_graph_phase_records(*renderer->counters());
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }
    phases.push_back({"rendering_loop", render_ms});
    phases.push_back({"encoder_close_and_flush", encode_ms});
    phases.push_back({"chronon_render_pure_ms", chronon_render_pure_ms});
    phases.push_back({"chronon_render_only_ms", chronon_render_only_ms});
    phases.push_back({"chronon_render_loop_ms", chronon_render_loop_ms});
    phases.push_back({"chronon_conversion_copy_ms", chronon_conversion_copy_ms});
    phases.push_back({"chronon_queue_wait_ms", chronon_queue_wait_ms});
    phases.push_back({"chronon_writer_encode_ms", chronon_writer_encode_ms});
    if (is_native) {
        phases.push_back({"native_av_convert_ms", native_convert_ms});
        phases.push_back({"native_av_send_frame_ms", native_send_ms});
        phases.push_back({"native_av_receive_packet_ms", native_receive_ms});
        phases.push_back({"native_av_mux_write_ms", native_mux_ms});
        phases.push_back({"native_av_trailer_ms", native_trailer_ms});
    } else {
        phases.push_back({"ffmpeg_encode_total_ms", ffmpeg_encode_total_ms});
        phases.push_back({"ffmpeg_flush_close_ms", ffmpeg_flush_close_ms});
    }
    phases.push_back({"e2e_wall_ms", wall_time_ms});

    // Fill system counters BEFORE capturing — otherwise process_cpu, RSS, RAM, TBB
    // counters won't make it into the SQLite render_counters table.
    if (renderer->counters()) {
        sys_metrics.fill_system_counters(*renderer->counters());
    }

    auto resolved_counters = telemetry::capture_counters(*renderer->counters());
    resolved_counters.push_back({"ffmpeg_pipe_write_blocked_duration_ms", static_cast<uint64_t>(std::llround(write_blocked_ms))});
    resolved_counters.push_back({"ffmpeg_queue_wait_duration_ms", static_cast<uint64_t>(std::llround(loop_result.queue_wait_ms))});

    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        auto pool_stats = sw_renderer->framebuffer_pool()->stats();
        resolved_counters.push_back({"framebuffer_pool_capacity", pool_stats.max_bytes});
        resolved_counters.push_back({"framebuffer_pool_available_count", pool_stats.available_count});
        resolved_counters.push_back({"framebuffer_pool_current_bytes", pool_stats.current_bytes});
        resolved_counters.push_back({"framebuffer_pool_total_allocations", pool_stats.total_allocations});
        resolved_counters.push_back({"framebuffer_pool_total_reuses", pool_stats.total_reuses});
    }

    const int encoded_frames = pipe_encoded_frame_count(export_status);
    cli::telemetry::record_output_run(
        composition_id, opts.output, export_status.success,
        total, encoded_frames, wall_time_ms, render_ms, encode_ms,
        started_at_iso, phases, resolved_counters,
        telemetry.node_events, renderer->counters(), telemetry_frames,
        telemetry.layer_events, telemetry.cache_events, telemetry.culling_events,
        telemetry.text_events, telemetry.image_events, telemetry.tile_events);

    // Populate the result boundary model
    result.success = export_status.success;
    result.cancelled = export_status.cancelled;
    result.render_failed = export_status.render_failed;
    result.writer_error = export_status.writer_error;
    result.exception_error = export_status.exception_error;
    result.frames_written = encoded_frames;
    result.wall_time_ms = wall_time_ms;
    result.render_ms = render_ms;
    result.encode_ms = encode_ms;
    result.return_code = export_status.success ? 0 : 1;

    if (!export_status.success) {
        log_pipe_export_failure(export_status);
        return result;
    }

    spdlog::info("[video] Wrote {}", opts.output);
    return result;
}

} // namespace chronon3d::cli
