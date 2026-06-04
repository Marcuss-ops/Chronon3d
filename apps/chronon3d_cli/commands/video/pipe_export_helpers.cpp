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

    const auto warmup_t0 = std::chrono::steady_clock::now();
    runtime::warmup_renderer(renderer, comp, runtime::RendererWarmupOptions{
        .width = comp.width(),
        .height = comp.height(),
        .framebuffer_count = opts.warmup_framebuffers,
        .preallocate_framebuffers = true,
        .touch_memory = true,
        .render_dummy_frame = opts.warmup_dummy_frame,
        .dummy_frame = 0,
        .quiet = false
    });
    const auto warmup_t1 = std::chrono::steady_clock::now();

    if (renderer.counters()) {
        const auto warmup_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(warmup_t1 - warmup_t0).count());
        renderer.counters()->setup_pool_preallocation_ms.fetch_add(warmup_ms, std::memory_order_relaxed);

        saved_fb_alloc = renderer.counters()->framebuffer_allocations.load(std::memory_order_relaxed);
        saved_fb_reuses = renderer.counters()->framebuffer_reuses.load(std::memory_order_relaxed);
        saved_fb_bytes = renderer.counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
        saved_fb_peak = renderer.counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);

        renderer.counters()->reset();

        renderer.counters()->framebuffer_allocations.store(saved_fb_alloc, std::memory_order_relaxed);
        renderer.counters()->framebuffer_reuses.store(saved_fb_reuses, std::memory_order_relaxed);
        renderer.counters()->framebuffer_bytes_allocated.store(saved_fb_bytes, std::memory_order_relaxed);
        renderer.counters()->framebuffer_bytes_peak.store(saved_fb_peak, std::memory_order_relaxed);
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
