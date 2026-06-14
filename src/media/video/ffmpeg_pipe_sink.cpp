#include "ffmpeg_pipe_sink.hpp"

#include <chronon3d/media/video/video_frame.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
//  build_argv() — construct ffmpeg command line as executable + arguments
// ============================================================================

std::vector<std::string> FfmpegPipeSink::build_argv(const VideoSinkConfig& config) {
    std::vector<std::string> argv;
    argv.reserve(32);

    argv.push_back("ffmpeg");
    // Quiet unless verbose (no verbose flag in VideoSinkConfig, default quiet).
    argv.push_back("-hide_banner");
    argv.push_back("-loglevel");
    argv.push_back("error");
    argv.push_back("-y");

    // Input: raw video via stdin pipe.
    argv.push_back("-f");
    argv.push_back("rawvideo");
    argv.push_back("-pix_fmt");
    argv.push_back(pix_fmt_to_ffmpeg(config.encoder.output_format));
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

    // Codec.
    argv.push_back("-c:v");
    argv.push_back(codec_to_string(config.encoder.codec));

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
    } else {
        // Default: zerolatency for pipe streaming.
        argv.push_back("-tune");
        argv.push_back("zerolatency");
    }

    // Output pixel format.
    argv.push_back("-pix_fmt");
    argv.push_back(pix_fmt_to_ffmpeg(config.encoder.output_format));

    // Color metadata (BT.709 for YUV).
    if (config.encoder.output_format == PixelFormat::YUV420P ||
        config.encoder.output_format == PixelFormat::NV12) {
        argv.push_back("-colorspace");
        argv.push_back("bt709");
        argv.push_back("-color_primaries");
        argv.push_back("bt709");
        argv.push_back("-color_trc");
        argv.push_back("bt709");
        argv.push_back("-color_range");
        argv.push_back("tv");
    }

    // Fast start for MP4.
    if (config.output.container == VideoContainer::Mp4) {
        argv.push_back("-movflags");
        argv.push_back("+faststart");
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
//  launch_ffmpeg() — spawn the subprocess
// ============================================================================

bool FfmpegPipeSink::launch_ffmpeg(const std::vector<std::string>& argv) {
    // Build command string from argv (popen requires a command string).
    std::string cmd;
    for (const auto& arg : argv) {
        if (!cmd.empty()) cmd += ' ';
        // Quote arguments containing spaces.
        if (arg.find(' ') != std::string::npos || arg.empty()) {
            cmd += '"' + arg + '"';
        } else {
            cmd += arg;
        }
    }

#if defined(_WIN32)
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
    if (pipe_) {
        // Full buffering: 1 MiB buffer reduces context switches.
        setvbuf(pipe_, nullptr, _IOFBF, 1 << 20);

        // Enlarge kernel pipe buffer to fit 2 frames, reducing backpressure.
        const int fd = fileno(pipe_);
        if (fd >= 0) {
            const int desired = static_cast<int>(
                std::clamp<size_t>(tight_frame_size_ * 2, 1ULL << 20, 16ULL << 20));
            fcntl(fd, F_SETPIPE_SZ, desired);
        }
    }
#endif

    if (!pipe_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    return true;
}

// ============================================================================
//  write_to_pipe() — write raw bytes, track telemetry
// ============================================================================

bool FfmpegPipeSink::write_to_pipe(const uint8_t* data, size_t size) {
    if (!pipe_ || pipe_failed_) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const size_t written = std::fwrite(data, 1, size, pipe_);
    const auto t1 = std::chrono::steady_clock::now();

    if (written != size) {
        pipe_failed_ = true;
        state_ = VideoSinkState::Failed;
        return false;
    }

    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    total_write_blocked_ms_ += elapsed;
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

    const auto& stream = config.stream;
    if (stream.width <= 0 || stream.height <= 0) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // YUV formats require even dimensions.
    const auto fmt = config.encoder.output_format;
    if (fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) {
        if (stream.width % 2 != 0 || stream.height % 2 != 0) {
            state_ = VideoSinkState::Failed;
            return false;
        }
    }

    // Store session contract.
    width_ = stream.width;
    height_ = stream.height;
    session_format_ = fmt;
    tight_frame_size_ = frame_buffer_size(fmt, stream.width, stream.height);

    // Build argv and launch ffmpeg.
    const auto argv = build_argv(config);
    if (!launch_ffmpeg(argv)) {
        return false;
    }

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

    if (session_format_ != PixelFormat::YUV420P) {
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

    if (session_format_ != PixelFormat::NV12) {
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

    if (pipe_ && !pipe_failed_) {
        std::fflush(pipe_);
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_flush_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

// ============================================================================
//  close()
// ============================================================================

bool FfmpegPipeSink::close() noexcept {
    if (state_ == VideoSinkState::Closed) {
        return true;
    }
    if (state_ == VideoSinkState::Created) {
        state_ = VideoSinkState::Closed;
        return true;
    }

    // If the pipe already failed internally, don't try to pclose —
    // the ffmpeg subprocess may be dead, and pclose() could hang.
    if (pipe_ && !pipe_failed_) {
        std::fflush(pipe_);
#if defined(_WIN32)
        const int rc = _pclose(pipe_);
#else
        const int rc = pclose(pipe_);
#endif
        pipe_ = nullptr;
        if (rc != 0) {
            state_ = VideoSinkState::Failed;
            return false;
        }
    } else if (pipe_) {
        // Pipe already failed — just close the FILE* without waiting.
#if defined(_WIN32)
        _pclose(pipe_);
#else
        // Terminate the child process rather than waiting.
        if (ffmpeg_pid_ > 0) {
            kill(ffmpeg_pid_, SIGTERM);
        }
        pclose(pipe_);
#endif
        pipe_ = nullptr;
        state_ = VideoSinkState::Failed;
        return false;
    }

    state_ = VideoSinkState::Closed;
    return true;
}

// ============================================================================
//  Destructor
// ============================================================================

FfmpegPipeSink::~FfmpegPipeSink() noexcept {
    if (pipe_) {
        std::fflush(pipe_);
#if defined(_WIN32)
        _pclose(pipe_);
#else
        pclose(pipe_);
#endif
        pipe_ = nullptr;
    }
}

} // namespace chronon3d::media::video
