#include "pipe_export_helpers.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/runtime/render_preparation.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <thread>

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
    return status.success ? status.frames_encoded : 0;
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
    return align_up(frame_bytes * 8ULL,
                    2ULL * 1024ULL * 1024ULL);
}

FfmpegPipeOptions make_pipe_options(
    const CompiledComposition& compiled,
    const FfmpegExportOptions& opts,
    const std::string& codec,
    const chronon3d::CpuBudget& cpu_budget)
{
    // "ultrafast"/"superfast" is x264 vocabulary. NVENC accepts p1..p7 and
    // the legacy slow/medium/fast names only, so the superfast→ultrafast
    // rewrite must never run when a hardware encoder is selected — passing
    // "ultrafast" to h264_nvenc would fail option validation at open.
    const bool x264_preset_vocab =
        opts.encoder.encoder_backend == "native" &&
        (opts.encoder.hardware_encoder.empty() ||
         opts.encoder.hardware_encoder == "none");
    const std::string effective_preset =
        (x264_preset_vocab && opts.encoder.encode_preset == "superfast")
            ? "ultrafast"
            : opts.encoder.encode_preset;
    const std::string effective_tune =
        (!opts.encoder.tune.empty())
            ? opts.encoder.tune
            : ((opts.encoder.codec == "libx264" && opts.encoder.encoder_backend != "native") ? "zerolatency" : "");

    FfmpegPipeOptions pipe_options{
        .width = compiled.composition->width(),
        .height = compiled.composition->height(),
        .fps = opts.output.fps,
        .fps_num = opts.output.fps_num,
        .fps_den = opts.output.fps_den,
        .rate_control_mode = opts.encoder.rate_control_mode,
        .rate_control_mode_explicit = opts.encoder.rate_control_mode_explicit,
        .crf = opts.encoder.crf,
        .crf_explicit = opts.encoder.crf_explicit,
        .qp = opts.encoder.qp,
        .qp_explicit = opts.encoder.qp_explicit,
        .bitrate = opts.encoder.bitrate,
        .bitrate_explicit = opts.encoder.bitrate_explicit,
        .async_depth = opts.encoder.async_depth,
        .preset = effective_preset,
        .preset_explicit = opts.encoder.encode_preset_explicit,
        .codec = codec,
        .hardware_encoder = opts.encoder.hardware_encoder,
        .output_path = opts.output.output,
        .input_format = parse_pipe_pixfmt(opts.pipe.pipe_pixfmt),
        .verbose = opts.pipe.ffmpeg_verbose,
        .color_transform = {
            .output = parse_color_output(opts.pipe.color_output),
        },
        .tune = effective_tune,
        .tune_explicit = !opts.encoder.tune.empty() && opts.encoder.tune_explicit,
        .pipe_writer = opts.pipe.pipe_writer,
        .audio_source_path = opts.gop_source,
    };
    pipe_options.output_pix_fmt = resolve_cli_ffmpeg_output_pix_fmt(codec);

    // Apply the unified CPU budget so the encoder respects the encode pool.
    // The writer thread itself is part of this pool, so reserve one thread
    // for it and give the remainder to the encoder internal thread pool.
    pipe_options.encode_threads = std::max(1, cpu_budget.encode_threads - 1);

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
    if (opts.encoder.encoder_backend == "native") {
        return;
    }

    const int pid = encoder.ffmpeg_pid();
    if (pid > 0) {
        sys_metrics.track_ffmpeg_pid(pid);
        spdlog::info("[video] Tracking FFmpeg child PID {} for system metrics", pid);
    }
}

chronon3d::Result<chronon3d::runtime::RendererWarmupResult,
                  chronon3d::runtime::PreparationError>
warmup_pipe_renderer(
    SoftwareRenderer & renderer,
    const CompiledComposition& compiled,
    const FfmpegExportOptions& opts,
    chronon3d::runtime::RenderPreparationTimings* out_timings)
{
    if (!opts.warmup.warmup_renderer) {
        return runtime::RendererWarmupResult{};
    }

    uint64_t saved_fb_alloc = 0;
    uint64_t saved_fb_reuses = 0;
    uint64_t saved_fb_bytes = 0;
    uint64_t saved_fb_peak = 0;

    const auto warmup_t0 = profiling::now();
    const auto preparation = runtime::prepare_render(
        &renderer, compiled,
        runtime::RenderPreparationOptions{
            .warmup_renderer = true,
            .warmup = runtime::RendererWarmupOptions{
                .width = compiled.composition->width(),
                .height = compiled.composition->height(),
                .framebuffer_count = opts.warmup.warmup_framebuffers,
                .preallocate_framebuffers = true,
                .touch_memory = true,
                .render_dummy_frame = opts.warmup.warmup_dummy_frame,
                .dummy_frame = 0,
                .quiet = false,
            },
        });
    if (!preparation.ok()) {
        spdlog::error("[video] Render preparation FAILED:\n{}",
                      preparation.diagnostic());
        if (preparation.preparation_error) {
            return *preparation.preparation_error;
        }
        return runtime::PreparationError{
            .code = runtime::PreparationError::Code::InternalError,
            .message = "render preparation failed without a structured error: " +
                preparation.diagnostic(),
            .phase = "render preparation",
        };
    }
    if (out_timings) {
        *out_timings = preparation.timings;
    }
    const auto warmup_t1 = profiling::now();

    if (renderer.counters()) {
        const auto warmup_ms = static_cast<uint64_t>(
            profiling::duration_ms(warmup_t0, warmup_t1));
        renderer.counters()->setup_pool_preallocation_wall_ms.fetch_add(warmup_ms, std::memory_order_relaxed);

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
        const uint64_t saved_node_exec = renderer.counters()->node_execute_actual_wall_ms.load(std::memory_order_relaxed);
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
        if (saved_node_exec > 0) renderer.counters()->node_execute_actual_wall_ms.store(saved_node_exec, std::memory_order_relaxed);
        if (saved_sys_cores > 0) renderer.counters()->system_logical_cores.store(saved_sys_cores, std::memory_order_relaxed);
        if (saved_cpu_user > 0) renderer.counters()->process_cpu_user_ms.store(saved_cpu_user, std::memory_order_relaxed);
        if (saved_cpu_sys > 0) renderer.counters()->process_cpu_sys_ms.store(saved_cpu_sys, std::memory_order_relaxed);
        if (saved_rss > 0) renderer.counters()->process_rss_peak_mb.store(saved_rss, std::memory_order_relaxed);
    }

    chronon3d::telemetry::clear_telemetry_stores();
    return preparation.warmup;
}

double pipe_write_blocked_ms(bool is_native, IVideoEncoder& encoder) {
    if (is_native) {
        return 0.0;
    }

    return encoder.total_write_blocked_ms();
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
