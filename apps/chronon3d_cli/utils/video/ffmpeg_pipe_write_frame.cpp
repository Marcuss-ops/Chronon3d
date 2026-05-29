#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>

#if defined(__linux__)
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace chronon3d::cli {

int encode_color_matrix_id(const FfmpegPipeOptions& options);

bool FfmpegPipeEncoder::write_frame(const Framebuffer& fb) {
    if (!pipe_ || pipe_failed_) {
        return false;
    }

    uint8_t* target_buffer = nullptr;
    size_t   bytes_to_write = 0;
    bool     ok = false;

    // Use the framebuffer content digest directly as the cache key.
    // The key_digest() uniquely identifies pixel content: identical frames
    // (e.g. consecutive static frames from the fast-path) share the same
    // digest and can reuse the already-converted encoder bytes without
    // re-running the float→RGBA/YUV conversion pipeline.
    const u64 frame_digest = fb.key_digest();
    const video::EncoderPixelFormat enc_fmt = [&]() -> video::EncoderPixelFormat {
        switch (options_.input_format) {
            case PipePixelFormat::YUV420P: return video::EncoderPixelFormat::YUV420P;
            case PipePixelFormat::NV12:    return video::EncoderPixelFormat::NV12;
            default:                       return video::EncoderPixelFormat::RGB24;
        }
    }();

    const bool can_cache = frame_digest != 0;

    const video::ConvertedFrameCacheKey cache_key{
        .framebuffer_digest = frame_digest,
        .width              = options_.width,
        .height             = options_.height,
        .format             = enc_fmt,
        .color_matrix       = encode_color_matrix_id(options_),
        .apply_gamma        = options_.color_transform.apply_gamma,
    };

    const auto t_conv0 = std::chrono::high_resolution_clock::now();

    if (can_cache) {
        const auto* hit = frame_cache_.lookup(cache_key);
        if (hit) {
            ok = true;

            if (profiling::g_current_counters) {
                profiling::g_current_counters->converted_frame_cache_hits
                    .fetch_add(1, std::memory_order_relaxed);
            }

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
                bytes_to_write = hit->data_size;
                std::memcpy(target_buffer, hit->data.data(), bytes_to_write);
                ring_buffer_pending_[buf_idx] = true;
                ring_buffer_bytes_written_[buf_idx] = 0;
                ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
            } else
#endif
            {
                target_buffer  = const_cast<uint8_t*>(hit->data.data());
                bytes_to_write = hit->data_size;
            }

            goto do_pipe_write;
        }
    }

    {
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
                        ring_buffer_pending_[buf_idx]       = true;
                        ring_buffer_bytes_written_[buf_idx] = 0;
                        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
                        bytes_to_write = ring_buffer_size_;
                    }
                } else
#endif
                {
                    const size_t req_size =
                        static_cast<size_t>(options_.width) *
                        static_cast<size_t>(options_.height) * 3u / 2u;
                    if (cached_frame_bytes_.size() < req_size + 256)
                        cached_frame_bytes_.resize(req_size + 256);
                    ok = convert_framebuffer_to_yuv420p(fb, cached_frame_bytes_.data());
                    if (ok) {
                        cached_frame_size_ = req_size;
                        target_buffer  = cached_frame_bytes_.data();
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
                        ring_buffer_pending_[buf_idx]       = true;
                        ring_buffer_bytes_written_[buf_idx] = 0;
                        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
                        bytes_to_write = ring_buffer_size_;
                    }
                } else
#endif
                {
                    const size_t req_size =
                        static_cast<size_t>(options_.width) *
                        static_cast<size_t>(options_.height) * 3u / 2u;
                    if (cached_frame_bytes_.size() < req_size + 256)
                        cached_frame_bytes_.resize(req_size + 256);
                    ok = convert_framebuffer_to_nv12(fb, cached_frame_bytes_.data());
                    if (ok) {
                        cached_frame_size_ = req_size;
                        target_buffer  = cached_frame_bytes_.data();
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
                        ring_buffer_pending_[buf_idx]       = true;
                        ring_buffer_bytes_written_[buf_idx] = 0;
                        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;
                        bytes_to_write = ring_buffer_size_;
                    }
                } else
#endif
                {
                    const size_t req_size =
                        static_cast<size_t>(options_.width) *
                        static_cast<size_t>(options_.height) * 4u;
                    if (cached_frame_bytes_.size() < req_size + 256)
                        cached_frame_bytes_.resize(req_size + 256);
                    ok = convert_framebuffer_to_rgba(fb, cached_frame_bytes_.data());
                    if (ok) {
                        cached_frame_size_ = req_size;
                        target_buffer  = cached_frame_bytes_.data();
                        bytes_to_write = cached_frame_size_;
                    }
                }
                break;
            }
            default:
                return false;
        }

        const auto t_conv1 = std::chrono::high_resolution_clock::now();
        const auto conv_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count());
        if (profiling::g_current_counters) {
            profiling::g_current_counters->video_conversion_ms
                .fetch_add(conv_ms, std::memory_order_relaxed);
            profiling::g_current_counters->frame_conversion_copy_ms
                .fetch_add(conv_ms, std::memory_order_relaxed);
        }

        if (ok && can_cache) {
            frame_cache_.insert(cache_key, target_buffer, bytes_to_write);
        }
    }

do_pipe_write:
    ++current_frame_;

    if (!ok) {
        return false;
    }

#ifdef __linux__
    if (use_uring_) {
        size_t prev_idx = (ring_buffer_index_ + kRingEntries - 1) % kRingEntries;
        const auto t_write0 = std::chrono::high_resolution_clock::now();
        bool written = write_uring(prev_idx, bytes_to_write);
        const auto t_write1 = std::chrono::high_resolution_clock::now();
        const auto write_ms = std::chrono::duration<double, std::milli>(t_write1 - t_write0).count();
        total_write_blocked_ms_ += write_ms;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->video_pipe_write_ms.fetch_add(
                static_cast<uint64_t>(write_ms), std::memory_order_relaxed);
        }
        if (!written) return false;
    } else
#endif
    {
        const auto t_write0 = std::chrono::high_resolution_clock::now();
        size_t res = std::fwrite(target_buffer, 1, bytes_to_write, pipe_);
        const auto t_write1 = std::chrono::high_resolution_clock::now();
        const auto write_ms = std::chrono::duration<double, std::milli>(t_write1 - t_write0).count();
        total_write_blocked_ms_ += write_ms;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->video_pipe_write_ms.fetch_add(
                static_cast<uint64_t>(write_ms), std::memory_order_relaxed);
        }
        if (res != bytes_to_write) {
            return false;
        }
        bytes_written_ += res;
    }

    ++frames_written_;
    return true;
}

} // namespace chronon3d::cli
