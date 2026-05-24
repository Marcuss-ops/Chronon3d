#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>
#include <thread>

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
    if ((options_.input_format == PipePixelFormat::YUV420P || options_.input_format == PipePixelFormat::NV12) &&
        (options_.width % 2 != 0 || options_.height % 2 != 0)) {
        return false;
    }

    frames_written_ = 0;
    bytes_written_ = 0;
    pipe_failed_ = false;

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);

    if (options_.input_format == PipePixelFormat::YUV420P || options_.input_format == PipePixelFormat::NV12) {
        const size_t size = w * h + (w * h) / 2u;
        yuv_buffer_.assign(size, 0);
#ifdef __linux__
        ring_buffer_size_ = size;
#endif
    } else {
        rgba_buffer_.assign(w * h * 4u, 0);
#ifdef __linux__
        ring_buffer_size_ = w * h * 4;
#endif
    }

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
    if (!pipe_ || pipe_failed_) {
        return false;
    }

    uint8_t* target_buffer = nullptr;
    size_t bytes_to_write = 0;
    bool ok = false;
    const u64 frame_digest = fb.key_digest();
    const bool can_cache_frame = options_.pipe_writer != "io_uring" && frame_digest != 0;
    const bool cache_hit = can_cache_frame &&
        cached_frame_valid_ &&
        cached_frame_digest_ == frame_digest &&
        cached_frame_format_ == options_.input_format &&
        cached_frame_size_ > 0;

    const auto t_conv0 = std::chrono::high_resolution_clock::now();
    if (cache_hit) {
        target_buffer = cached_frame_bytes_.data();
        bytes_to_write = cached_frame_size_;
        ok = true;
    } else {
        switch (options_.input_format) {
            case PipePixelFormat::YUV420P: {
#ifdef __linux__
                if (use_uring_) {
                    while (pending_writes_count_ >= kRingEntries) {
                        reap_completed_uring(true);
                    }
                    size_t buf_idx = ring_buffer_index_;
                    while (ring_buffer_pending_[buf_idx]) {
                        reap_completed_uring(true);
                        buf_idx = ring_buffer_index_;
                    }
                    target_buffer = ring_buffers_[buf_idx].data();
                    ok = convert_framebuffer_to_yuv420p(fb, target_buffer);
                    if (ok) {
                        ring_buffer_pending_[buf_idx] = true;
                        ring_buffer_bytes_written_[buf_idx] = 0;
                        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
                    }
                } else
#endif
                {
                    const size_t req_size = static_cast<size_t>(options_.width) * static_cast<size_t>(options_.height) * 3u / 2u;
                    cached_frame_bytes_.resize(req_size);
                    ok = convert_framebuffer_to_yuv420p(fb, cached_frame_bytes_.data());
                    if (ok) {
                        cached_frame_size_ = req_size;
                        target_buffer = cached_frame_bytes_.data();
                        bytes_to_write = cached_frame_size_;
                    }
                }
                break;
            }
            case PipePixelFormat::NV12: {
#ifdef __linux__
                if (use_uring_) {
                    while (pending_writes_count_ >= kRingEntries) {
                        reap_completed_uring(true);
                    }
                    size_t buf_idx = ring_buffer_index_;
                    while (ring_buffer_pending_[buf_idx]) {
                        reap_completed_uring(true);
                        buf_idx = ring_buffer_index_;
                    }
                    target_buffer = ring_buffers_[buf_idx].data();
                    ok = convert_framebuffer_to_nv12(fb, target_buffer);
                    if (ok) {
                        ring_buffer_pending_[buf_idx] = true;
                        ring_buffer_bytes_written_[buf_idx] = 0;
                        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
                    }
                } else
#endif
                {
                    const size_t req_size = static_cast<size_t>(options_.width) * static_cast<size_t>(options_.height) * 3u / 2u;
                    cached_frame_bytes_.resize(req_size);
                    ok = convert_framebuffer_to_nv12(fb, cached_frame_bytes_.data());
                    if (ok) {
                        cached_frame_size_ = req_size;
                        target_buffer = cached_frame_bytes_.data();
                        bytes_to_write = cached_frame_size_;
                    }
                }
                break;
            }
            case PipePixelFormat::RGBA: {
#ifdef __linux__
                if (use_uring_) {
                    while (pending_writes_count_ >= kRingEntries) {
                        reap_completed_uring(true);
                    }
                    size_t buf_idx = ring_buffer_index_;
                    while (ring_buffer_pending_[buf_idx]) {
                        reap_completed_uring(true);
                        buf_idx = ring_buffer_index_;
                    }
                    target_buffer = ring_buffers_[buf_idx].data();
                    ok = convert_framebuffer_to_rgba(fb, target_buffer);
                    if (ok) {
                        ring_buffer_pending_[buf_idx] = true;
                        ring_buffer_bytes_written_[buf_idx] = 0;
                        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
                    }
                } else
#endif
                {
                    const size_t req_size = static_cast<size_t>(options_.width) * static_cast<size_t>(options_.height) * 4u;
                    cached_frame_bytes_.resize(req_size);
                    ok = convert_framebuffer_to_rgba(fb, cached_frame_bytes_.data());
                    if (ok) {
                        cached_frame_size_ = req_size;
                        target_buffer = cached_frame_bytes_.data();
                        bytes_to_write = cached_frame_size_;
                    }
                }
                break;
            }
            default:
                return false;
        }

        if (ok && !use_uring_) {
            cached_frame_digest_ = frame_digest;
            cached_frame_format_ = options_.input_format;
            cached_frame_valid_ = true;
        }
    }
    const auto t_conv1 = std::chrono::high_resolution_clock::now();
    if (profiling::g_current_counters) {
        const auto conv_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count());
        profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(conv_ms,
            std::memory_order_relaxed);
        profiling::g_current_counters->video_conversion_ms.fetch_add(conv_ms,
            std::memory_order_relaxed);
    }

    if (!ok) {
        return false;
    }

#ifdef __linux__
    if (use_uring_) {
        size_t prev_idx = (ring_buffer_index_ + kRingEntries - 1) % kRingEntries;
        const auto t_write0 = std::chrono::high_resolution_clock::now();
        bool written = write_uring(prev_idx, ring_buffer_size_);
        const auto t_write1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t_write1 - t_write0).count();
        if (!written) return false;
    } else
#endif
    {
        const auto t_write0 = std::chrono::high_resolution_clock::now();
        size_t res = std::fwrite(target_buffer, 1, bytes_to_write, pipe_);
        const auto t_write1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t_write1 - t_write0).count();
        if (res != bytes_to_write) {
            return false;
        }
        bytes_written_ += res;
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
