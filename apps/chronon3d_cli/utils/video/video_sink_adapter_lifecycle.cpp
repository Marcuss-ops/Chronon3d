#include "video_sink_adapter.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool VideoSinkEncoderAdapter::open(const FfmpegPipeOptions& options) {
    if (sink_) {
        spdlog::error("[video_adapter] open() called when already open");
        return false;
    }
    if (options.width <= 0 || options.height <= 0 || options.canonical_fps_num() <= 0 ||
        options.canonical_fps_den() <= 0) {
        spdlog::error("[video_adapter] Invalid encoder options (w={}, h={}, fps={}/{})",
                      options.width, options.height, options.canonical_fps_num(),
                      options.canonical_fps_den());
        return false;
    }

    options_ = options;
    width_ = options.width;
    height_ = options.height;
    input_format_ = options.input_format;
    codec_ = options.codec;
    output_pix_fmt_ = options.output_pix_fmt;

    chronon3d::media::video::VideoSinkConfig config;
    if (!build_sink_config(options, config)) return false;
    sink_ = chronon3d::media::video::create_video_sink(config);
    if (!sink_) {
        spdlog::error("[video_adapter] create_video_sink() returned nullptr");
        return false;
    }
    if (!sink_->open(config)) {
        spdlog::error("[video_adapter] sink->open() failed: {} — {}",
                      to_string(sink_->last_error()), sink_->last_error_message());
        sink_.reset();
        return false;
    }

    video::EncoderPixelFormat pool_format;
    switch (options.input_format) {
        case PipePixelFormat::YUV420P: pool_format = video::EncoderPixelFormat::YUV420P; break;
        case PipePixelFormat::NV12: pool_format = video::EncoderPixelFormat::NV12; break;
        case PipePixelFormat::RGBA:
        default: pool_format = video::EncoderPixelFormat::RGBA8; break;
    }
    encoder_pool_ = std::make_unique<video::EncoderFramePool>(
        video::EncoderFramePool::Config{
            .width = width_, .height = height_, .format = pool_format, .slot_count = 4});
    if (encoder_pool_->frame_bytes() == 0) {
        spdlog::error("[video_adapter] invalid encoder frame pool configuration");
        sink_->close();
        sink_.reset();
        encoder_pool_.reset();
        return false;
    }

    frames_written_ = 0;
    write_blocked_ms_ = 0.0;
    last_telemetry_ = EncoderFrameTelemetry{};
    if (sink_) {
        auto diag = sink_->diagnostics();
        ffmpeg_pid_ = diag.child_pid;
    } else {
        ffmpeg_pid_ = -1;
    }

    const char* fmt_str = "rgba";
    if (options.input_format == PipePixelFormat::YUV420P) fmt_str = "yuv420p";
    else if (options.input_format == PipePixelFormat::NV12) fmt_str = "nv12";
    spdlog::info("[video_adapter] Opened {} sink: {}x{} @ {}fps, format={}, codec={}",
                 (sink_type_ == VideoSinkType::RawFile) ? "raw" : "ffmpeg-pipe",
                 width_, height_, options.fps, fmt_str, options.codec);
    return true;
}

bool VideoSinkEncoderAdapter::close() {
    if (!sink_) return true;
    const auto close_t0 = profiling::now();
    bool flush_ok = sink_->flush();
    if (!flush_ok) {
        spdlog::warn("[video_adapter] sink->flush() reported failure: {} — {}",
                     to_string(sink_->last_error()), sink_->last_error_message());
    }
    bool close_ok = sink_->close();
    if (!close_ok) {
        spdlog::error("[video_adapter] sink->close() failed: {} — {}",
                      to_string(sink_->last_error()), sink_->last_error_message());
    }
    const double close_ms = profiling::duration_ms(close_t0, profiling::now());
    spdlog::info("[video_adapter] Closed sink — {} frames written, write_blocked={:.2f}ms, close={:.2f}ms",
                 frames_written_, write_blocked_ms_, close_ms);
    sink_.reset();
    return close_ok;
}

VideoSinkEncoderAdapter::~VideoSinkEncoderAdapter() noexcept {
    if (sink_) {
        try { close(); } catch (...) {}
    }
}

} // namespace chronon3d::cli
