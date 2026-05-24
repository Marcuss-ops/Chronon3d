#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>

#if defined(_WIN32)
    #include <io.h>
#endif

namespace chronon3d::cli {

bool FfmpegPipeEncoder::open(const FfmpegPipeOptions& options) {
    if (pipe_) {
        return false;
    }

    if (options.width <= 0 || options.height <= 0 ||
        options.fps <= 0 || options.output_path.empty()) {
        return false;
    }

    options_ = options;
    frames_written_ = 0;
    bytes_written_ = 0;

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);

    switch (options_.input_format) {
        case PipePixelFormat::RGBA:
            rgba_buffer_.assign(w * h * 4u, 0);
            break;
        case PipePixelFormat::YUV420P:
            yuv_buffer_.assign(w * h * 3u / 2u, 0);
            break;
        case PipePixelFormat::NV12:
            yuv_buffer_.assign(w * h * 3u / 2u, 0);
            break;
    }

#ifdef __linux__
    ring_buffer_size_ = (options_.input_format == PipePixelFormat::RGBA) ? (w * h * 4) : (w * h * 3 / 2);
#endif

    const std::string cmd = build_ffmpeg_raw_pipe_command(options_);

#if defined(_WIN32)
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
    if (pipe_) {
        setvbuf(pipe_, nullptr, _IOFBF, 1 << 20);
    }
#endif

#ifdef __linux__
    if (pipe_ && options_.pipe_writer == "io_uring") {
        if (init_uring()) {
            use_uring_ = true;
        }
    }
#endif

    return pipe_ != nullptr;
}

bool FfmpegPipeEncoder::write_frame(const Framebuffer& fb) {
    if (!pipe_) {
        return false;
    }

#ifdef __linux__
    if (use_uring_) {
        // Wait for slot to become free if all busy
        while (pending_writes_count_ >= kRingEntries) {
            reap_completed_uring(true);
        }

        // Find the next free buffer in pool
        size_t buf_idx = ring_buffer_index_;
        while (ring_buffer_pending_[buf_idx]) {
            reap_completed_uring(true);
            buf_idx = ring_buffer_index_;
        }

        uint8_t* dst = ring_buffers_[buf_idx].data();
        bool ok = false;
        const auto t_conv0 = std::chrono::high_resolution_clock::now();
        if (options_.input_format == PipePixelFormat::RGBA) {
            ok = convert_framebuffer_to_rgba(fb, dst);
        } else if (options_.input_format == PipePixelFormat::YUV420P) {
            ok = convert_framebuffer_to_yuv420p(fb, dst);
        } else if (options_.input_format == PipePixelFormat::NV12) {
            ok = convert_framebuffer_to_nv12(fb, dst);
        }
        const auto t_conv1 = std::chrono::high_resolution_clock::now();
        if (profiling::g_current_counters) {
            profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(
                static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count()),
                std::memory_order_relaxed);
        }

        if (!ok) return false;

        ring_buffer_pending_[buf_idx] = true;
        ring_buffer_bytes_written_[buf_idx] = 0;
        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;

        const auto t0 = std::chrono::high_resolution_clock::now();
        bool written = write_uring(buf_idx, ring_buffer_size_);
        const auto t1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!written) return false;

        ++frames_written_;
        return true;
    }
#endif

    auto timed_fwrite = [&](const void* ptr, size_t size, size_t count, FILE* stream) -> size_t {
        const auto t0 = std::chrono::high_resolution_clock::now();
        size_t res = std::fwrite(ptr, size, count, stream);
        const auto t1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
        return res;
    };

    switch (options_.input_format) {
        case PipePixelFormat::RGBA: {
            const auto t_conv0 = std::chrono::high_resolution_clock::now();
            if (!convert_framebuffer_to_rgba(fb)) {
                return false;
            }
            const auto t_conv1 = std::chrono::high_resolution_clock::now();
            if (profiling::g_current_counters) {
                profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(
                    static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count()),
                    std::memory_order_relaxed);
            }
            const size_t written = timed_fwrite(
                rgba_buffer_.data(), 1, rgba_buffer_.size(), pipe_);
            if (written != rgba_buffer_.size()) {
                return false;
            }
            bytes_written_ += written;
            break;
        }
        case PipePixelFormat::YUV420P: {
            const auto t_conv0 = std::chrono::high_resolution_clock::now();
            if (yuv_buffer_.empty() || !convert_framebuffer_to_yuv420p(fb, yuv_buffer_.data())) {
                return false;
            }
            const auto t_conv1 = std::chrono::high_resolution_clock::now();
            if (profiling::g_current_counters) {
                profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(
                    static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count()),
                    std::memory_order_relaxed);
            }
            const size_t written = timed_fwrite(yuv_buffer_.data(), 1, yuv_buffer_.size(), pipe_);
            if (written != yuv_buffer_.size()) {
                return false;
            }
            bytes_written_ += written;
            break;
        }
        case PipePixelFormat::NV12: {
            const auto t_conv0 = std::chrono::high_resolution_clock::now();
            if (yuv_buffer_.empty() || !convert_framebuffer_to_nv12(fb, yuv_buffer_.data())) {
                return false;
            }
            const auto t_conv1 = std::chrono::high_resolution_clock::now();
            if (profiling::g_current_counters) {
                profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(
                    static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count()),
                    std::memory_order_relaxed);
            }
            const size_t written = timed_fwrite(yuv_buffer_.data(), 1, yuv_buffer_.size(), pipe_);
            if (written != yuv_buffer_.size()) {
                return false;
            }
            bytes_written_ += written;
            break;
        }
    }

    ++frames_written_;
    return true;
}

bool FfmpegPipeEncoder::close() {
    if (!pipe_) {
        return true;
    }

#ifdef __linux__
    if (use_uring_) {
        while (pending_writes_count_ > 0) {
            reap_completed_uring(true);
        }
        cleanup_uring();
    }
#endif

#if defined(_WIN32)
    const int rc = _pclose(pipe_);
#else
    const int rc = pclose(pipe_);
#endif

    pipe_ = nullptr;
    return rc == 0;
}

FfmpegPipeEncoder::~FfmpegPipeEncoder() {
    if (pipe_) {
        close();
    }
}

} // namespace chronon3d::cli
