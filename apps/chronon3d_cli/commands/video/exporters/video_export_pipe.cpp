#include "../common/pipe_export_setup.hpp"
#include "../common/pipe_export_close.hpp"
#include "../common/pipe_export_telemetry.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "../common/pipe_export_session.hpp"

#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>
#include <atomic>
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
    const int total = static_cast<int>(end - start);
    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();

    // ── Pre-chunk warning ───────────────────────────────────────────────
    if (opts.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 1: Setup
    // ═══════════════════════════════════════════════════════════════════
    auto setup = setup_pipe_export(registry, comp, settings, opts);
    if (!setup) {
        result.return_code = 1;
        return result;
    }

    if (!ensure_output_directory_exists(opts.output)) {
        result.return_code = 1;
        return result;
    }

    auto& encoder = *setup->encoder;
    auto& renderer = *setup->renderer;
    auto* sw_renderer = setup->sw_renderer;
    auto& node_cache = setup->node_cache;
    auto& triple_arena = setup->triple_arena;
    auto& queue = setup->queue;
    auto& writer_failed = setup->writer_failed;
    auto& writer_done = setup->writer_done;
    auto& sys_metrics = setup->sys_metrics;

    const double setup_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - wall_t0).count();

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2: Writer thread
    // ═══════════════════════════════════════════════════════════════════
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    std::vector<FrameEncoderTelemetryRecord> frame_encoder_telemetry;
    telemetry_frames.reserve(total > 0 ? static_cast<size_t>(total) : 0);
    frame_encoder_telemetry.reserve(total > 0 ? static_cast<size_t>(total) : 0);
    std::atomic<uint64_t> writer_encode_us_total{0};

    WriterThreadContext writer_ctx{
        .queue = queue,
        .writer_failed = writer_failed,
        .writer_done = writer_done,
        .triple_arena = triple_arena,
        .encoder = encoder,
        .renderer = *sw_renderer,
        .writer_encode_us_total = writer_encode_us_total,
        .frame_encoder_telemetry = frame_encoder_telemetry,
    };
    std::thread writer_thread(run_writer_thread, std::ref(writer_ctx));

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 3: Warmup + prealloc
    // ═══════════════════════════════════════════════════════════════════
    RenderSettings render_opts = settings;
    render_opts.use_modular_graph = true;

    warmup_pipe_renderer(*sw_renderer, comp, opts);

    // Pre-warm the framebuffer pool with canvas-sized buffers
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

    sys_metrics.sample_cpu_start();

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 4: Render loop
    // ═══════════════════════════════════════════════════════════════════
    const auto render_t0 = std::chrono::steady_clock::now();

    RenderLoopContext loop_ctx{
        .backend = renderer,
        .node_cache = node_cache,
        .settings = render_opts,
        .registry = registry,
        .video_decoder = setup->video_decoder,
        .comp = comp,
        .start = start,
        .end = end,
        .opts = opts,
        .sw_renderer = sw_renderer,
        .queue = queue,
        .writer_failed = writer_failed,
        .triple_arena = triple_arena,
        .counters = renderer.counters(),
        .telemetry_frames = telemetry_frames,
    };
    auto loop_result = run_render_loop(loop_ctx);

    const auto render_t1 = std::chrono::steady_clock::now();
    const double render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 5: Teardown (writer join, encoder close)
    // ═══════════════════════════════════════════════════════════════════
    writer_done.store(true);
    if (writer_thread.joinable()) {
        writer_thread.join();
    }

    PipeExportStatus& export_status = loop_result.status;
    if (writer_failed.load()) {
        export_status.success = false;
        export_status.writer_error = true;
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    const double encode_ms = std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();

    const auto bench = close_pipe_export(
        encoder, renderer.counters(), render_ms, encode_ms, wall_time_ms,
        setup_ms, loop_result.render_graph_eval_ms, loop_result.queue_wait_ms,
        writer_encode_us_total, opts.encoder_backend);

    bool encoder_closed = encoder.close();
    if (!encoder_closed) {
        spdlog::error("[video] Encoder close failed");
        export_status.success = false;
        result.encoder_close_failed = true;
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 6: Telemetry
    // ═══════════════════════════════════════════════════════════════════
    merge_encoder_telemetry(telemetry_frames, frame_encoder_telemetry);

    std::sort(telemetry_frames.begin(), telemetry_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    auto telemetry = chronon3d::telemetry::collect_all_telemetry();

    const bool is_native = (opts.encoder_backend == "native");
    const auto phases = build_pipe_export_phases(bench, renderer.counters(), is_native);

    // Fill system counters BEFORE capturing
    if (renderer.counters()) {
        sys_metrics.fill_system_counters(*renderer.counters());
    }

    const auto resolved_counters = build_pipe_export_counters(
        *renderer.counters(), sw_renderer, bench.write_blocked_ms, bench.queue_wait_ms);

    record_pipe_export_run(
        composition_id, opts.output, export_status, total, bench,
        started_at_iso, phases, resolved_counters, telemetry,
        renderer.counters());

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 7: Result
    // ═══════════════════════════════════════════════════════════════════
    result.success = export_status.success;
    result.cancelled = export_status.cancelled;
    result.render_failed = export_status.render_failed;
    result.writer_error = export_status.writer_error;
    result.exception_error = export_status.exception_error;
    result.frames_written = pipe_encoded_frame_count(export_status);
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
