#include "pipe_export_helpers.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

namespace {

constexpr size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

bool should_log_pipe_progress(int done_count, int total) {
    return done_count % std::max(1, total / 10) == 0 || done_count == total;
}

int pipe_encoded_frame_count(const PipeExportStatus& status) {
    return status.success ? status.frames_written : 0;
}

void mark_pipe_cancelled(PipeExportStatus& status, Frame frame) {
    spdlog::warn("[video] Render cancelled at frame {}", frame);
    status.success = false;
    status.cancelled = true;
}

void mark_pipe_writer_failed(PipeExportStatus& status, Frame frame) {
    spdlog::error("[video] FFmpeg writer failed before frame {}", frame);
    status.success = false;
    status.writer_error = true;
}

void mark_pipe_render_failed(PipeExportStatus& status, Frame frame) {
    spdlog::error("[video] Failed to render frame {}", frame);
    status.success = false;
    status.render_failed = true;
}

void mark_pipe_exception(PipeExportStatus& status, Frame frame, const std::exception& error) {
    spdlog::error("[video] Exception during render loop (frame {}): {}", frame, error.what());
    status.success = false;
    status.exception_error = true;
}

size_t compute_pipe_arena_size(int width, int height) {
    const size_t frame_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(Color);
    return align_up(std::max<size_t>(128ULL * 1024ULL * 1024ULL, frame_bytes * 8ULL),
                    2ULL * 1024ULL * 1024ULL);
}

FfmpegPipeOptions make_pipe_options(
    const Composition& comp,
    const FfmpegExportOptions& opts,
    const std::string& codec)
{
    const std::string effective_preset =
        (opts.encoder_backend == "native" && opts.encode_preset == "superfast")
            ? "ultrafast"
            : opts.encode_preset;
    const std::string effective_tune =
        (!opts.tune.empty())
            ? opts.tune
            : ((opts.codec == "libx264" && opts.encoder_backend != "native") ? "zerolatency" : "");

    FfmpegPipeOptions pipe_options{
        .width = comp.width(),
        .height = comp.height(),
        .fps = opts.fps,
        .crf = opts.crf,
        .preset = effective_preset,
        .codec = codec,
        .output_path = opts.output,
        .input_format = parse_pipe_pixfmt(opts.pipe_pixfmt),
        .verbose = opts.ffmpeg_verbose,
        .color_transform = {
            .output = parse_color_output(opts.color_output),
        },
        .tune = effective_tune,
        .pipe_writer = opts.pipe_writer,
    };
    pipe_options.output_pix_fmt = resolve_cli_ffmpeg_output_pix_fmt(codec);
    return pipe_options;
}

bool ensure_output_directory_exists(const std::string& output_path) {
    std::error_code ec;
    const auto output_parent = std::filesystem::path(output_path).parent_path();
    if (!output_parent.empty()) {
        std::filesystem::create_directories(output_parent, ec);
        if (ec) {
            spdlog::error("[video] Cannot create output directory {}: {}", output_parent.string(), ec.message());
            return false;
        }
    }
    return true;
}

void track_pipe_encoder_process(
    const FfmpegExportOptions& opts,
    IVideoEncoder& encoder,
    SystemMetricsCollector& sys_metrics)
{
    if (opts.encoder_backend == "native") {
        return;
    }

    auto* pipe_enc = dynamic_cast<FfmpegPipeEncoder*>(&encoder);
    if (!pipe_enc) {
        return;
    }

    sys_metrics.track_ffmpeg_pid(pipe_enc->ffmpeg_pid());
    if (pipe_enc->ffmpeg_pid() > 0) {
        spdlog::info("[video] Tracking FFmpeg child PID {} for system metrics", pipe_enc->ffmpeg_pid());
    }
}

void warmup_pipe_renderer(
    SoftwareRenderer& renderer,
    const Composition& comp,
    const FfmpegExportOptions& opts)
{
    if (!opts.warmup_renderer) {
        return;
    }

    uint64_t saved_fb_alloc = 0;
    uint64_t saved_fb_reuses = 0;
    uint64_t saved_fb_bytes = 0;
    uint64_t saved_fb_peak = 0;
    uint64_t saved_focus_in_ladder_warmup = 0;

    const auto warmup_t0 = std::chrono::steady_clock::now();
    auto warmup_result = runtime::warmup_renderer(renderer, comp, runtime::RendererWarmupOptions{
        .width = comp.width(),
        .height = comp.height(),
        .framebuffer_count = opts.warmup_framebuffers,
        .preallocate_framebuffers = true,
        .touch_memory = true,
        .render_dummy_frame = opts.warmup_dummy_frame,
        .dummy_frame = 0,
        .quiet = false,
        .warmup_focus_in_ladder = opts.warmup_focus_in_ladder
    });
    const auto warmup_t1 = std::chrono::steady_clock::now();

    // Capture FocusInLadder precompute time before counter reset
    saved_focus_in_ladder_warmup = static_cast<uint64_t>(
        std::llround(warmup_result.focus_in_ladder_precompute_ms));

    if (renderer.counters()) {
        const auto warmup_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(warmup_t1 - warmup_t0).count());
        renderer.counters()->setup_pool_preallocation_ms.fetch_add(warmup_ms, std::memory_order_relaxed);

        // Save ALL counters before reset so we can restore non-framebuffer ones
        saved_fb_alloc = renderer.counters()->framebuffer_allocations.load(std::memory_order_relaxed);
        saved_fb_reuses = renderer.counters()->framebuffer_reuses.load(std::memory_order_relaxed);
        saved_fb_bytes = renderer.counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
        saved_fb_peak = renderer.counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);

        // Save parallelism and system counters — reset() clears everything
        const uint64_t saved_tbb_peak = renderer.counters()->tbb_active_workers_peak.load(std::memory_order_relaxed);
        const uint64_t saved_tbb_avg_sum = renderer.counters()->tbb_active_workers_avg_sum.load(std::memory_order_relaxed);
        const uint64_t saved_tbb_avg_cnt = renderer.counters()->tbb_active_workers_avg_count.load(std::memory_order_relaxed);
        const uint64_t saved_tbb_arena = renderer.counters()->tbb_arena_max_concurrency.load(std::memory_order_relaxed);
        const uint64_t saved_pcount = renderer.counters()->parallel_regions_count.load(std::memory_order_relaxed);
        const uint64_t saved_pskip = renderer.counters()->parallel_regions_skipped_small_level.load(std::memory_order_relaxed);
        const uint64_t saved_lpar = renderer.counters()->level_parallel_count.load(std::memory_order_relaxed);
        const uint64_t saved_lseq = renderer.counters()->level_sequential_count.load(std::memory_order_relaxed);
        const uint64_t saved_used_clear = renderer.counters()->used_parallel_clear.load(std::memory_order_relaxed);
        const uint64_t saved_used_xform = renderer.counters()->used_parallel_transform.load(std::memory_order_relaxed);
        const uint64_t saved_used_comp = renderer.counters()->used_parallel_composite.load(std::memory_order_relaxed);
        const uint64_t saved_skip_clear = renderer.counters()->skipped_clear_small.load(std::memory_order_relaxed);
        const uint64_t saved_skip_xform = renderer.counters()->skipped_transform_small.load(std::memory_order_relaxed);
        const uint64_t saved_skip_comp = renderer.counters()->skipped_composite_small.load(std::memory_order_relaxed);
        const uint64_t saved_node_exec = renderer.counters()->node_execute_actual_ms.load(std::memory_order_relaxed);
        const uint64_t saved_sys_cores = renderer.counters()->system_logical_cores.load(std::memory_order_relaxed);
        const uint64_t saved_cpu_user = renderer.counters()->process_cpu_user_ms.load(std::memory_order_relaxed);
        const uint64_t saved_cpu_sys = renderer.counters()->process_cpu_sys_ms.load(std::memory_order_relaxed);
        const uint64_t saved_rss = renderer.counters()->process_rss_peak_mb.load(std::memory_order_relaxed);

        renderer.counters()->reset();

        // Restore framebuffer stats
        renderer.counters()->framebuffer_allocations.store(saved_fb_alloc, std::memory_order_relaxed);
        renderer.counters()->framebuffer_reuses.store(saved_fb_reuses, std::memory_order_relaxed);
        renderer.counters()->framebuffer_bytes_allocated.store(saved_fb_bytes, std::memory_order_relaxed);
        renderer.counters()->framebuffer_bytes_peak.store(saved_fb_peak, std::memory_order_relaxed);

        // Restore parallelism and system counters from warmup
        // These are accumulated across warmup + main render for accurate telemetry.
        if (saved_tbb_peak > 0) renderer.counters()->tbb_active_workers_peak.store(saved_tbb_peak, std::memory_order_relaxed);
        if (saved_tbb_avg_sum > 0) renderer.counters()->tbb_active_workers_avg_sum.store(saved_tbb_avg_sum, std::memory_order_relaxed);
        if (saved_tbb_avg_cnt > 0) renderer.counters()->tbb_active_workers_avg_count.store(saved_tbb_avg_cnt, std::memory_order_relaxed);
        if (saved_tbb_arena > 0) renderer.counters()->tbb_arena_max_concurrency.store(saved_tbb_arena, std::memory_order_relaxed);
        if (saved_pcount > 0) renderer.counters()->parallel_regions_count.store(saved_pcount, std::memory_order_relaxed);
        if (saved_pskip > 0) renderer.counters()->parallel_regions_skipped_small_level.store(saved_pskip, std::memory_order_relaxed);
        if (saved_lpar > 0) renderer.counters()->level_parallel_count.store(saved_lpar, std::memory_order_relaxed);
        if (saved_lseq > 0) renderer.counters()->level_sequential_count.store(saved_lseq, std::memory_order_relaxed);
        if (saved_used_clear > 0) renderer.counters()->used_parallel_clear.store(saved_used_clear, std::memory_order_relaxed);
        if (saved_used_xform > 0) renderer.counters()->used_parallel_transform.store(saved_used_xform, std::memory_order_relaxed);
        if (saved_used_comp > 0) renderer.counters()->used_parallel_composite.store(saved_used_comp, std::memory_order_relaxed);
        if (saved_skip_clear > 0) renderer.counters()->skipped_clear_small.store(saved_skip_clear, std::memory_order_relaxed);
        if (saved_skip_xform > 0) renderer.counters()->skipped_transform_small.store(saved_skip_xform, std::memory_order_relaxed);
        if (saved_skip_comp > 0) renderer.counters()->skipped_composite_small.store(saved_skip_comp, std::memory_order_relaxed);
        if (saved_node_exec > 0) renderer.counters()->node_execute_actual_ms.store(saved_node_exec, std::memory_order_relaxed);
        if (saved_sys_cores > 0) renderer.counters()->system_logical_cores.store(saved_sys_cores, std::memory_order_relaxed);
        if (saved_cpu_user > 0) renderer.counters()->process_cpu_user_ms.store(saved_cpu_user, std::memory_order_relaxed);
        if (saved_cpu_sys > 0) renderer.counters()->process_cpu_sys_ms.store(saved_cpu_sys, std::memory_order_relaxed);
        if (saved_rss > 0) renderer.counters()->process_rss_peak_mb.store(saved_rss, std::memory_order_relaxed);
        if (saved_focus_in_ladder_warmup > 0) renderer.counters()->effect_focus_in_ladder_warmup_ms.store(
            saved_focus_in_ladder_warmup, std::memory_order_relaxed);
    }

    chronon3d::telemetry::clear_telemetry_stores();
}

double pipe_write_blocked_ms(bool is_native, IVideoEncoder& encoder) {
    if (is_native) {
        return 0.0;
    }

    auto* pipe_enc = dynamic_cast<FfmpegPipeEncoder*>(&encoder);
    if (pipe_enc) {
        return pipe_enc->total_write_blocked_ms();
    }
    return 0.0;
}

void log_pipe_export_failure(const PipeExportStatus& status) {
    spdlog::error(
        "[video] Export incomplete: cancelled={} render_failed={} writer_error={} exception={}",
        status.cancelled,
        status.render_failed,
        status.writer_error,
        status.exception_error
    );
}

} // namespace chronon3d::cli
