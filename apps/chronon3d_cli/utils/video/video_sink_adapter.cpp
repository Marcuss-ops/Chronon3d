#include "video_sink_adapter.hpp"

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

// ============================================================================
//  Config mapping helpers
// ============================================================================

chronon3d::media::video::VideoCodec
VideoSinkEncoderAdapter::map_codec(const std::string& codec) {
    using media::video::VideoCodec;

    // Hardware codecs are not available in CPU-only mode — warn and fall
    // back to the equivalent software encoder.
    if (codec == "h264_nvenc" || codec == "h264_amf" ||
        codec == "h264_qsv" || codec == "h264_videotoolbox") {
        spdlog::warn("[video_adapter] Hardware codec '{}' is unavailable in CPU-only mode; "
                      "falling back to libx264", codec);
        return VideoCodec::H264;
    }
    if (codec == "hevc_nvenc" || codec == "hevc_amf" ||
        codec == "hevc_qsv" || codec == "hevc_videotoolbox") {
        spdlog::warn("[video_adapter] Hardware codec '{}' is unavailable in CPU-only mode; "
                      "falling back to libx265", codec);
        return VideoCodec::H265;
    }

    if (codec == "libx264" || codec == "h264") {
        return VideoCodec::H264;
    }
    if (codec == "libx265" || codec == "h265" || codec == "hevc") {
        return VideoCodec::H265;
    }
    if (codec == "libvpx-vp9" || codec == "vp9") {
        return VideoCodec::VP9;
    }
    if (codec == "libaom-av1" || codec == "av1") {
        return VideoCodec::AV1;
    }
    if (codec == "uncompressed" || codec == "raw") {
        return VideoCodec::Uncompressed;
    }
    return VideoCodec::Auto;
}

chronon3d::media::video::PixelFormat
VideoSinkEncoderAdapter::map_pixel_format(PipePixelFormat fmt) {
    using media::video::PixelFormat;
    switch (fmt) {
        case PipePixelFormat::RGBA:    return PixelFormat::RGBA8;
        case PipePixelFormat::YUV420P: return PixelFormat::YUV420P;
        case PipePixelFormat::NV12:    return PixelFormat::NV12;
    }
    return PixelFormat::RGBA8;
}

chronon3d::media::video::PixelFormat
VideoSinkEncoderAdapter::map_output_pix_fmt(const std::string& fmt) {
    using media::video::PixelFormat;
    if (fmt == "yuv420p") return PixelFormat::YUV420P;
    if (fmt == "nv12")    return PixelFormat::NV12;
    if (fmt == "rgb24")   return PixelFormat::RGB24;
    if (fmt == "rgba")    return PixelFormat::RGBA8;
    return PixelFormat::YUV420P;
}

chronon3d::media::video::VideoContainer
VideoSinkEncoderAdapter::detect_container(const std::string& path) {
    using media::video::VideoContainer;
    const auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".mp4" || ext == ".MP4")  return VideoContainer::Mp4;
    if (ext == ".mkv" || ext == ".MKV")  return VideoContainer::Mkv;
    if (ext == ".webm" || ext == ".WEBM") return VideoContainer::WebM;
    if (ext == ".raw" || ext == ".yuv")  return VideoContainer::Raw;
    return VideoContainer::Mp4;
}

// ============================================================================
//  build_sink_config() — FfmpegPipeOptions → VideoSinkConfig
// ============================================================================

bool VideoSinkEncoderAdapter::build_sink_config(
    const FfmpegPipeOptions& opts,
    chronon3d::media::video::VideoSinkConfig& config)
{
    using media::video::VideoCodec;
    using media::video::VideoContainer;

    // ── Stream config ──────────────────────────────────────────────────
    config.stream.width            = opts.width;
    config.stream.height           = opts.height;
    config.stream.frame_rate_num   = opts.fps;
    config.stream.frame_rate_den   = 1;
    config.stream.submitted_format = map_pixel_format(opts.input_format);

    // ── Encoder config ─────────────────────────────────────────────────
    // For RawFile sink: override codec to Uncompressed + container to Raw.
    if (sink_type_ == VideoSinkType::RawFile) {
        config.encoder.codec = VideoCodec::Uncompressed;
        config.encoder.crf   = -1;
        config.encoder.bitrate = 0;
    } else {
        const auto container = detect_container(opts.output_path);
        auto raw_codec = map_codec(opts.codec);
        // Resolve Auto to a concrete codec based on container.
        config.encoder.codec = media::video::resolve_auto_codec(raw_codec, container);
        config.encoder.crf      = opts.crf;
        config.encoder.bitrate  = 0;  // CLI doesn't set bitrate (CRF-based)
        config.encoder.preset   = opts.preset;
        config.encoder.tune     = opts.tune.empty()
            ? std::nullopt
            : std::make_optional(opts.tune);
    }

    config.encoder.encoded_pixel_format = map_output_pix_fmt(opts.output_pix_fmt);
    config.encoder.apply_gamma = opts.color_transform.apply_gamma;

    // ── Transport config ───────────────────────────────────────────────
    // The old pipeline was synchronous (blocking writes).  Match that
    // behaviour until the async path is fully tested.
    config.transport.asynchronous   = false;
    config.transport.queue_capacity = 4;
    // io_uring is not wired in the current sinks — silently fall back to
    // classic synchronous writes.
    if (opts.pipe_writer == "io_uring") {
        spdlog::warn("[video_adapter] io_uring pipe writer is not implemented; "
                      "falling back to classic synchronous pipe");
    }
    config.transport.use_io_uring   = false;
    config.transport.write_timeout  = std::chrono::milliseconds(30000);

    // ── Output config ──────────────────────────────────────────────────
    if (sink_type_ == VideoSinkType::RawFile) {
        config.output.container = VideoContainer::Raw;
    } else {
        config.output.container = detect_container(opts.output_path);
    }
    config.output.output_path = std::filesystem::path(opts.output_path);
    config.output.overwrite   = true;  // always overwrite like the old CLI

    config.label = (sink_type_ == VideoSinkType::RawFile)
        ? "adapter-raw-video-sink"
        : "adapter-ffmpeg-pipe-sink";

    return true;
}

// ============================================================================
//  open() — create sink, open it
// ============================================================================

bool VideoSinkEncoderAdapter::open(const FfmpegPipeOptions& options) {
    if (sink_) {
        spdlog::error("[video_adapter] open() called when already open");
        return false;
    }

    if (options.width <= 0 || options.height <= 0 || options.fps <= 0) {
        spdlog::error("[video_adapter] Invalid encoder options (w={}, h={}, fps={})",
                      options.width, options.height, options.fps);
        return false;
    }

    options_ = options;
    width_   = options.width;
    height_  = options.height;
    input_format_ = options.input_format;
    codec_   = options.codec;
    output_pix_fmt_ = options.output_pix_fmt;

    // ── Build VideoSinkConfig ──────────────────────────────────────────
    chronon3d::media::video::VideoSinkConfig config;
    if (!build_sink_config(options, config)) {
        return false;
    }

    // ── Create sink via factory ────────────────────────────────────────
    sink_ = chronon3d::media::video::create_video_sink(config);
    if (!sink_) {
        spdlog::error("[video_adapter] create_video_sink() returned nullptr");
        return false;
    }

    // ── Open sink ──────────────────────────────────────────────────────
    if (!sink_->open(config)) {
        spdlog::error("[video_adapter] sink->open() failed: {} — {}",
                      to_string(sink_->last_error()), sink_->last_error_message());
        sink_.reset();
        return false;
    }

    // ── Pre-allocate staging buffer ────────────────────────────────────
    const size_t bytes_per_frame = static_cast<size_t>(
        chronon3d::media::video::frame_buffer_size(
            config.stream.submitted_format,
            config.stream.width,
            config.stream.height));
    if (bytes_per_frame > 0) {
        staging_buffer_.reserve(bytes_per_frame + 256);
        staging_buffer_.resize(bytes_per_frame);
    }

    // ── Reset state ────────────────────────────────────────────────────
    frames_written_   = 0;
    write_blocked_ms_ = 0.0;
    last_telemetry_   = EncoderFrameTelemetry{};

    // Read the PID from the underlying sink via diagnostics().
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

// ============================================================================
//  write_frame() / convert_and_submit()
// ============================================================================

bool VideoSinkEncoderAdapter::convert_and_submit(const Framebuffer& fb) {
    if (!sink_) {
        return false;
    }

    // ── 1. Convert Framebuffer → tight-packed pixel buffer ─────────────
    using media::video::PixelFormat;
    const auto fmt = map_pixel_format(input_format_);

    // Map our PipePixelFormat to EncoderPixelFormat for the conversion service.
    video::EncoderPixelFormat enc_fmt;
    switch (input_format_) {
        case PipePixelFormat::YUV420P: enc_fmt = video::EncoderPixelFormat::YUV420P; break;
        case PipePixelFormat::NV12:    enc_fmt = video::EncoderPixelFormat::NV12; break;
        case PipePixelFormat::RGBA:
        default:                      enc_fmt = video::EncoderPixelFormat::RGBA8; break;
    }

    const size_t expected_size = static_cast<size_t>(
        chronon3d::media::video::frame_buffer_size(fmt, width_, height_));

    // Ensure staging buffer is large enough.
    if (staging_buffer_.size() < expected_size) {
        staging_buffer_.resize(expected_size);
    }

    const auto conv_t0 = profiling::now();

    // Use FrameConversionService to convert.
    const video::ConversionOptions copts{
        .width       = width_,
        .height      = height_,
        .format      = enc_fmt,
        .apply_gamma = options_.color_transform.apply_gamma,
        .matrix      = video::YuvMatrix::BT709,
        .range       = video::ColorRange::Limited,
        .use_cache   = false,  // caching across frames is encoder-level, not per-submit
    };

    auto converted = conv_svc_.convert_into(
        fb, copts, staging_buffer_.data(), expected_size);

    const auto conv_t1 = profiling::now();
    const double conv_ms = profiling::duration_ms(conv_t0, conv_t1);

    if (!converted) {
        spdlog::error("[video_adapter] Frame conversion failed");
        return false;
    }

    // Track conversion telemetry.
    last_telemetry_.conversion_copy_ms = conv_ms;

    if (counters_) {
        counters_->video_conversion_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->frame_conversion_copy_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->video_frames_converted.fetch_add(1, std::memory_order_relaxed);
    }

    // ── 2. Build VideoFrameView ────────────────────────────────────────
    // For YUV420P/NV12 the conversion service produces tight-packed data:
    //   YUV420P: Y(w×h) + U(w/2×h/2) + V(w/2×h/2) — contiguous
    //   NV12:    Y(w×h) + UV(w×h/2) — contiguous
    //   RGBA8:   RGBA(w×h×4) — contiguous
    // For the new sink, packed formats use submit(VideoFrameView) directly.
    chronon3d::media::video::VideoFrameView view;
    view.data          = converted.data.data();
    view.stride_bytes  = 0;  // tight packing
    view.width         = width_;
    view.height        = height_;
    view.pixel_format  = fmt;
    view.pts           = static_cast<int64_t>(frames_written_);

    // ── 3. Submit to sink ──────────────────────────────────────────────
    const auto submit_t0 = std::chrono::steady_clock::now();
    // The conversion service produces tight-packed data for all formats.
    // For YUV420P: Y(w×h) + U(w/2×h/2) + V(w/2×h/2) — contiguous
    // For NV12:    Y(w×h) + UV(w×h/2) — contiguous
    // For RGBA8:   RGBA(w×h×4) — contiguous
    // The sink's submit() handles all packed formats uniformly.
    bool ok = sink_->submit(view);
    const auto submit_t1 = std::chrono::steady_clock::now();

    if (!ok) {
        spdlog::error("[video_adapter] sink->submit() failed at frame {}: {} — {}",
                      frames_written_,
                      to_string(sink_->last_error()), sink_->last_error_message());
        return false;
    }

    const double submit_ms = std::chrono::duration<double, std::milli>(
        submit_t1 - submit_t0).count();
    last_telemetry_.encoder_ms = submit_ms;
    write_blocked_ms_ += submit_ms;

    if (counters_) {
        counters_->video_frames_submitted.fetch_add(1, std::memory_order_relaxed);
        counters_->video_pipe_write_ms.fetch_add(
            static_cast<uint64_t>(submit_ms), std::memory_order_relaxed);
        counters_->video_frames_written_counter.fetch_add(
            1, std::memory_order_relaxed);
    }

    ++frames_written_;
    return true;
}

bool VideoSinkEncoderAdapter::write_frame(const Framebuffer& fb) {
    if (!sink_) {
        return false;
    }
    return convert_and_submit(fb);
}

bool VideoSinkEncoderAdapter::write_frame_async(
    const Framebuffer& fb,
    std::shared_ptr<Framebuffer> owner)
{
    const bool ok = write_frame(fb);
    owner.reset();
    return ok;
}

// ============================================================================
//  close()
// ============================================================================

bool VideoSinkEncoderAdapter::close() {
    if (!sink_) {
        return true;  // already closed or never opened
    }

    // ── Flush + close the sink ──────────────────────────────────────────
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

    const auto close_t1 = profiling::now();
    const double close_ms = profiling::duration_ms(close_t0, close_t1);

    spdlog::info("[video_adapter] Closed sink — {} frames written, write_blocked={:.2f}ms, close={:.2f}ms",
                 frames_written_, write_blocked_ms_, close_ms);

    sink_.reset();
    return close_ok;
}

// ============================================================================
//  Destructor
// ============================================================================

VideoSinkEncoderAdapter::~VideoSinkEncoderAdapter() noexcept {
    if (sink_) {
        try {
            close();
        } catch (...) {
            // Destructor must not throw.
        }
    }
}

} // namespace chronon3d::cli
