#include "ffmpeg_pipe_sink.hpp"

#include <chronon3d/media/video/video_frame.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace chronon3d::media::video {

// ============================================================================
//  Map VideoCodec → ffmpeg codec string
// ============================================================================

namespace {

[[nodiscard]] const char* codec_to_string(VideoCodec codec) noexcept {
    switch (codec) {
        case VideoCodec::H264:  return "libx264";
        case VideoCodec::H265:  return "libx265";
        case VideoCodec::VP9:   return "libvpx-vp9";
        case VideoCodec::AV1:   return "libaom-av1";
        case VideoCodec::Auto:
        default:                return "libx264";
    }
}

[[nodiscard]] const char* container_extension(VideoContainer c) noexcept {
    switch (c) {
        case VideoContainer::Mkv:   return ".mkv";
        case VideoContainer::WebM:  return ".webm";
        case VideoContainer::Raw:   return ".raw";
        case VideoContainer::Mp4:
        default:                    return ".mp4";
    }
}

[[nodiscard]] const char* pix_fmt_to_ffmpeg(PixelFormat fmt) noexcept {
    switch (fmt) {
        case PixelFormat::YUV420P: return "yuv420p";
        case PixelFormat::NV12:    return "nv12";
        case PixelFormat::RGB24:   return "rgb24";
        case PixelFormat::RGBA8:   return "rgba";
    }
    return "yuv420p";
}

} // anonymous namespace

// ============================================================================
//  build_argv() — construct ffmpeg argv vector (executable + arguments)
// ============================================================================

std::vector<std::string> FfmpegPipeSink::build_argv(const VideoSinkConfig& config) {
    std::vector<std::string> argv;
    argv.reserve(32);

    argv.push_back("ffmpeg");
    // Quiet unless verbose (no verbose flag in VideoSinkConfig, default quiet).
    argv.push_back("-hide_banner");
    argv.push_back("-loglevel");
    argv.push_back("error");
    // Overwrite.
    if (config.output.overwrite) {
        argv.push_back("-y");
    } else {
        argv.push_back("-n");
    }

    // Input: raw video via stdin pipe.
    argv.push_back("-f");
    argv.push_back("rawvideo");
    argv.push_back("-pix_fmt");
    argv.push_back(pix_fmt_to_ffmpeg(config.stream.submitted_format));
    argv.push_back("-s");
    argv.push_back(std::to_string(config.stream.width) + "x" +
                   std::to_string(config.stream.height));
    const auto fps_str = std::to_string(config.stream.frame_rate_num)
                       + "/" + std::to_string(std::max(1, config.stream.frame_rate_den));
    argv.push_back("-r");
    argv.push_back(fps_str);
    argv.push_back("-i");
    argv.push_back("-");

    // No audio.
    argv.push_back("-an");

    // Codec — resolve Auto based on container before converting to string.
    const auto resolved_codec = resolve_auto_codec(
        config.encoder.codec, config.output.container);
    argv.push_back("-c:v");
    argv.push_back(codec_to_string(resolved_codec));

    // CRF / quality.
    if (config.encoder.crf >= 0) {
        argv.push_back("-crf");
        argv.push_back(std::to_string(config.encoder.crf));
    }

    // Preset.
    if (!config.encoder.preset.empty()) {
        argv.push_back("-preset");
        argv.push_back(config.encoder.preset);
    }

    // Tune.
    if (config.encoder.tune.has_value() && !config.encoder.tune->empty()) {
        argv.push_back("-tune");
        argv.push_back(*config.encoder.tune);
    }

    // Bitrate.
    if (config.encoder.bitrate > 0) {
        argv.push_back("-b:v");
        argv.push_back(std::to_string(config.encoder.bitrate));
    }

    // Output (encoded) pixel format.
    argv.push_back("-pix_fmt");
    argv.push_back(pix_fmt_to_ffmpeg(config.encoder.encoded_pixel_format));

    // Color metadata (BT.709 for YUV).
    if (config.encoder.encoded_pixel_format == PixelFormat::YUV420P ||
        config.encoder.encoded_pixel_format == PixelFormat::NV12) {
        argv.push_back("-colorspace");
        argv.push_back("bt709");
        argv.push_back("-color_primaries");
        argv.push_back("bt709");
        argv.push_back("-color_trc");
        argv.push_back("bt709");
        argv.push_back("-color_range");
        argv.push_back("tv");
    }

    // Fragmented MP4 for pipe output (+faststart requires seek, not possible on pipes).
    if (config.output.container == VideoContainer::Mp4) {
        argv.push_back("-movflags");
        argv.push_back("frag_keyframe+empty_moov");
    }

    // Output path.
    std::string output_path = config.output.output_path.string();
    // Append container extension if the filename component has no dot.
    const auto filename = std::filesystem::path(output_path).filename().string();
    if (filename.find('.') == std::string::npos) {
        output_path += container_extension(config.output.container);
    }
    argv.push_back(output_path);

    return argv;
}

// ============================================================================
//  launch_ffmpeg() — spawn via ProcessRunner (posix_spawnp, no shell)
// ============================================================================

bool FfmpegPipeSink::launch_ffmpeg(const std::vector<std::string>& argv) {
    // argv[0] is "ffmpeg" — use as both executable name and argv[0].
    const bool ok = process_.launch(argv[0], argv);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    return true;
}

// ============================================================================
//  write_to_pipe() — write raw bytes, track telemetry
// ============================================================================

bool FfmpegPipeSink::write_to_pipe(const uint8_t* data, size_t size) {
    if (pipe_failed_) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    // Always use write_for() — it handles O_NONBLOCK correctly via poll().
    // When write_timeout is 0 (no deadline), write_for() substitutes a
    // large default internally so the write never spuriously fails on
    // EAGAIN.
    const bool ok = process_.write_for(data, size, write_timeout_);

    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    total_write_blocked_ms_ += elapsed;

    if (!ok) {
        pipe_failed_ = true;
        state_ = VideoSinkState::Failed;
        return false;
    }

    stats_.bytes_written += size;
    return true;
}

// ============================================================================
//  validate_format() — check frame format against session contract
// ============================================================================

bool FfmpegPipeSink::validate_format(const VideoFrameView& frame) const noexcept {
    if (frame.pixel_format != session_format_) {
        return false;
    }
    if (frame.width != width_ || frame.height != height_) {
        return false;
    }
    return true;
}

// ============================================================================
//  open()
// ============================================================================

bool FfmpegPipeSink::open(const VideoSinkConfig& config) {
    if (state_ != VideoSinkState::Created) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Centralised config validation — reject invalid configurations early.
    const auto validation = validate_video_sink_config(config);
    if (!validation) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto& stream = config.stream;
    const auto fmt = config.stream.submitted_format;

    // Pre-check: if overwrite=false and file exists, fail early.
    // (The central validator doesn't check file existence — that's a
    //  runtime condition, not a configuration invariant.)
    if (!config.output.overwrite) {
        const auto output_path = config.output.output_path;
        if (std::filesystem::exists(output_path)) {
            state_ = VideoSinkState::Failed;
            return false;
        }
    }

    // Store session contract (the format callers submit frames in).
    width_ = stream.width;
    height_ = stream.height;
    session_format_ = fmt;
    tight_frame_size_ = frame_buffer_size(fmt, stream.width, stream.height);

    // Build argv and launch ffmpeg.
    const auto argv = build_argv(config);
    if (!launch_ffmpeg(argv)) {
        return false;
    }

    // Store the write timeout from config.
    write_timeout_ = config.transport.write_timeout;

    // Reset stats.
    stats_ = Stats{};
    total_write_blocked_ms_ = 0.0;
    pipe_failed_ = false;
    state_ = VideoSinkState::Open;
    return true;
}

// ============================================================================
//  submit() — interleaved/packed frame
// ============================================================================

bool FfmpegPipeSink::submit(const VideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || pipe_failed_) {
        return false;
    }

    if (!frame.data || !validate_format(frame)) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto* data = static_cast<const uint8_t*>(frame.data);
    const size_t tight_row_bytes = frame_buffer_size(
        frame.pixel_format, frame.width, 1);
    const size_t actual_stride = (frame.stride_bytes > 0)
        ? frame.stride_bytes
        : tight_row_bytes;

    // Validate stride (must be >= tight row; YUV planar only tight).
    if (!validate_packed_stride(frame.pixel_format, frame.width,
                                 frame.stride_bytes)) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    bool ok;
    if (actual_stride == tight_row_bytes) {
        // Tight packing.
        ok = write_to_pipe(data, tight_frame_size_);
    } else {
        // Row padding: strip stride row-by-row.
        const auto* row = data;
        for (int y = 0; y < frame.height; ++y) {
            ok = write_to_pipe(row, tight_row_bytes);
            if (!ok) break;
            row += actual_stride;
        }
    }

    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_submit_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.submit_count++;
    stats_.frames_submitted++;
    return true;
}

// ============================================================================
//  submit_planar() — YUV420P (pack into interleaved, write)
// ============================================================================

bool FfmpegPipeSink::submit_planar(const PlanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || pipe_failed_) {
        return false;
    }

    if (!frame.y_data || !frame.u_data || !frame.v_data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate dimensions and format.
    if (session_format_ != PixelFormat::YUV420P) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (!validate_planar_frame(frame.width, frame.height,
                                frame.y_stride, frame.u_stride, frame.v_stride)) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (frame.width != width_ || frame.height != height_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const size_t y_size  = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    const size_t y_row  = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width / 2);
    const size_t y_stride  = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t u_stride  = (frame.u_stride > 0) ? frame.u_stride : uv_row;
    const size_t v_stride  = (frame.v_stride > 0) ? frame.v_stride : uv_row;

    // Copy Y.
    if (y_stride == y_row) {
        std::memcpy(staging_.data(), frame.y_data, y_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.y_data);
        auto* dst = staging_.data();
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(dst, src, y_row);
            src += y_stride;
            dst += y_row;
        }
    }

    // Copy U.
    auto* u_dst = staging_.data() + y_size;
    if (u_stride == uv_row) {
        std::memcpy(u_dst, frame.u_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.u_data);
        auto* dst = u_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, uv_row);
            src += u_stride;
            dst += uv_row;
        }
    }

    // Copy V.
    auto* v_dst = staging_.data() + y_size + uv_size;
    if (v_stride == uv_row) {
        std::memcpy(v_dst, frame.v_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.v_data);
        auto* dst = v_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, uv_row);
            src += v_stride;
            dst += uv_row;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = write_to_pipe(staging_.data(), total);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_submit_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.submit_count++;
    stats_.frames_submitted++;
    return true;
}

// ============================================================================
//  submit_biplanar() — NV12 (pack into interleaved, write)
// ============================================================================

bool FfmpegPipeSink::submit_biplanar(const BiplanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || pipe_failed_) {
        return false;
    }

    if (!frame.y_data || !frame.uv_data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate dimensions and format.
    if (session_format_ != PixelFormat::NV12) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (!validate_biplanar_frame(frame.width, frame.height,
                                  frame.y_stride, frame.uv_stride)) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (frame.width != width_ || frame.height != height_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const size_t y_size  = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    const size_t uv_size = y_size / 2;
    const size_t total   = y_size + uv_size;

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    const size_t y_row  = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width);
    const size_t y_stride  = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t uv_stride = (frame.uv_stride > 0) ? frame.uv_stride : uv_row;

    // Copy Y.
    if (y_stride == y_row) {
        std::memcpy(staging_.data(), frame.y_data, y_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.y_data);
        auto* dst = staging_.data();
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(dst, src, y_row);
            src += y_stride;
            dst += y_row;
        }
    }

    // Copy UV.
    auto* uv_dst = staging_.data() + y_size;
    if (uv_stride == uv_row) {
        std::memcpy(uv_dst, frame.uv_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.uv_data);
        auto* dst = uv_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, uv_row);
            src += uv_stride;
            dst += uv_row;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = write_to_pipe(staging_.data(), total);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_submit_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.submit_count++;
    stats_.frames_submitted++;
    return true;
}

// ============================================================================
//  flush()
// ============================================================================

bool FfmpegPipeSink::flush() {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    // fd-based writes have no userspace buffering; flush is effectively
    // a no-op.  We still track timing for telemetry parity.
    // The kernel pipe buffer is drained asynchronously by the child.

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_flush_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

// ============================================================================
//  close()
// ============================================================================

bool FfmpegPipeSink::close() noexcept {
    // Preserve the Failed state so it isn't overwritten to Closed below.
    // This matters when submit() set Failed (e.g. invalid frame format)
    // but the pipe itself is still intact — close() must still report failure.
    const bool was_failed = (state_ == VideoSinkState::Failed);

    if (state_ == VideoSinkState::Closed) {
        return true;
    }
    if (state_ == VideoSinkState::Created) {
        state_ = VideoSinkState::Closed;
        return true;
    }

    // Normal path: close stdin (signals EOF) and wait for ffmpeg to exit.
    // Use a generous 30s timeout — ffmpeg may need time to encode and mux
    // the last frames before its encoder flushes the output.
    if (!pipe_failed_) {
        const int exit_code = process_.wait_for(std::chrono::seconds(30));
        if (exit_code == -2) {
            // Timeout — escalate.
            process_.terminate_and_wait(std::chrono::seconds(5));
            const std::string stderr_log = process_.consume_stderr();
            if (!stderr_log.empty()) {
                spdlog::warn("[FfmpegPipeSink] ffmpeg timed out — stderr:\n{}",
                             stderr_log);
            }
            state_ = VideoSinkState::Failed;
            return false;
        }
        if (exit_code != 0) {
            const std::string stderr_log = process_.consume_stderr();
            if (!stderr_log.empty()) {
                spdlog::error("[FfmpegPipeSink] ffmpeg exited with code {} — stderr:\n{}",
                              exit_code, stderr_log);
            } else {
                spdlog::error("[FfmpegPipeSink] ffmpeg exited with code {} (no stderr)",
                              exit_code);
            }
            state_ = VideoSinkState::Failed;
            return false;
        }
    } else {
        // Pipe failed — terminate the child gracefully, then escalate.
        process_.terminate_and_wait(std::chrono::seconds(5));
        const std::string stderr_log = process_.consume_stderr();
        if (!stderr_log.empty()) {
            spdlog::error("[FfmpegPipeSink] pipe broken — ffmpeg stderr:\n{}",
                          stderr_log);
        }
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Restore Failed if it was set before close(), e.g. by a submit() that
    // rejected an invalid frame format without breaking the pipe.
    state_ = was_failed ? VideoSinkState::Failed : VideoSinkState::Closed;
    return !was_failed;
}

// ============================================================================
//  Destructor
// ============================================================================

FfmpegPipeSink::~FfmpegPipeSink() noexcept {
    // ProcessRunner destructor handles cleanup: close stdin, terminate,
    // wait for child.  Nothing extra needed here.
}

} // namespace chronon3d::media::video
