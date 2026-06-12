#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chronon3d/core/profiling/profiling.hpp>

#if defined(__linux__)
    #include <fcntl.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <sched.h>
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

    const auto t_conv0 = profiling::now();

    double frame_conv_ms = 0.0;
    double frame_write_ms = 0.0;
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
                    // cached_frame_bytes_ is pre-allocated in open() —
                    // no resize needed in the hot path.
                    const size_t req_size =
                        static_cast<size_t>(options_.width) *
                        static_cast<size_t>(options_.height) * 3u / 2u;
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
                    // cached_frame_bytes_ is pre-allocated in open().
                    const size_t req_size =
                        static_cast<size_t>(options_.width) *
                        static_cast<size_t>(options_.height) * 3u / 2u;
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
                    // cached_frame_bytes_ is pre-allocated in open().
                    const size_t req_size =
                        static_cast<size_t>(options_.width) *
                        static_cast<size_t>(options_.height) * 4u;
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

        const auto t_conv1 = profiling::now();
        const auto conv_ms = static_cast<uint64_t>(
            profiling::duration_ms(t_conv0, t_conv1));
        frame_conv_ms = static_cast<double>(conv_ms);
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
        const auto t_write0 = profiling::now();
        bool written = write_uring(prev_idx, bytes_to_write);
        const auto t_write1 = profiling::now();
        const auto write_ms = profiling::duration_ms(t_write0, t_write1);
        frame_write_ms = write_ms;
        total_write_blocked_ms_ += write_ms;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->video_pipe_write_ms.fetch_add(
                static_cast<uint64_t>(write_ms), std::memory_order_relaxed);
        }
        if (!written) return false;
    } else
#endif
    {
        const auto t_write0 = profiling::now();
        size_t res = std::fwrite(target_buffer, 1, bytes_to_write, pipe_);
        const auto t_write1 = profiling::now();
        const auto write_ms = profiling::duration_ms(t_write0, t_write1);
        frame_write_ms = write_ms;
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

    last_frame_telemetry_ = {
        .conversion_copy_ms = frame_conv_ms,
        .encoder_ms = frame_write_ms,
        .pipe_write_ms = frame_write_ms,
    };
    ++frames_written_;
    return true;
}

// ── Async ring-buffer conversion (producer: writer thread, consumer: converter thread) ──

bool FfmpegPipeEncoder::write_frame_async(const Framebuffer& fb,
                                            std::shared_ptr<Framebuffer> owner) {
    if (!pipe_ || pipe_failed_) {
        return false;
    }

    // Compute cache key (same logic as write_frame)
    const uint64_t frame_digest = fb.key_digest();
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

    // ── Reserve an empty slot (CAS 0→1) ───────────────────────────────────
    // State machine: 0=empty, 1=reserved, 2=staged, 3=converted
    size_t idx = producer_idx_.load(std::memory_order_relaxed);
    for (;;) {
        if (pipe_failed_) {
            return false;
        }
        int expected = 0;
        if (async_slots_[idx].state.compare_exchange_strong(
                expected, 1,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            break;
        }
        // Slot not empty — try to write the next converted frame to free up space
        if (!write_next_converted_frame() && pipe_failed_) {
            return false;
        }
        std::this_thread::yield();
        idx = (idx + 1) % kAsyncConversionSlots;
    }

    // ── Write slot data (sequenced-after the CAS) ────────────────────────
    // The converter thread only reads slots with state==2, so the data
    // is guaranteed to be visible because the release fence + store(2)
    // below synchronizes with the converter's acquire load.
    auto& slot = async_slots_[idx];
    slot.fb = std::move(owner);
    slot.cache_key = cache_key;
    slot.can_cache = can_cache;
    const size_t req_size =
        (options_.input_format == PipePixelFormat::RGBA)
            ? static_cast<size_t>(options_.width) *
              static_cast<size_t>(options_.height) * 4u
            : static_cast<size_t>(options_.width) *
              static_cast<size_t>(options_.height) * 3u / 2u;
    if (slot.converted.size() < req_size) {
        slot.converted.resize(req_size);
    }

    // Release fence ensures all slot data writes are visible before the
    // converter thread sees state==2.
    std::atomic_thread_fence(std::memory_order_release);
    slot.state.store(2, std::memory_order_release);

    producer_idx_.store((idx + 1) % kAsyncConversionSlots,
                         std::memory_order_relaxed);

    // Start converter thread if not running
    if (!converter_thread_.joinable()) {
        converter_stop_.store(false, std::memory_order_release);
        converter_thread_ =
            std::thread(&FfmpegPipeEncoder::converter_loop, this);
    }

    // Try to write any already-converted frames to the pipe
    write_next_converted_frame();

    return true;
}

void FfmpegPipeEncoder::converter_loop() {
#if defined(__linux__)
    // Pin converter thread to the last core to avoid cache migration and
    // reduce contention with the render thread's TBB workers.
    const int total_cores = static_cast<int>(std::thread::hardware_concurrency());
    if (total_cores > 1) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(total_cores - 1, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
#endif

    while (!converter_stop_.load(std::memory_order_relaxed)) {
        size_t idx = consumer_idx_.load(std::memory_order_relaxed);
        auto& slot = async_slots_[idx];
        if (slot.state.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
            continue;
        }

        const size_t req_size =
            (options_.input_format == PipePixelFormat::RGBA)
                ? static_cast<size_t>(options_.width) *
                  static_cast<size_t>(options_.height) * 4u
                : static_cast<size_t>(options_.width) *
                  static_cast<size_t>(options_.height) * 3u / 2u;

        // ── Cache hit fast path ────────────────────────────────────────
        bool converted = false;
        if (slot.can_cache) {
            const auto* hit = frame_cache_.lookup(slot.cache_key);
            if (hit) {
                if (frame_counters_) {
                    frame_counters_->converted_frame_cache_hits
                        .fetch_add(1, std::memory_order_relaxed);
                }
                if (slot.converted.size() < hit->data_size) {
                    slot.converted.resize(hit->data_size);
                }
                std::memcpy(slot.converted.data(),
                            hit->data.data(), hit->data_size);
                converted = true;
            }
        }

        // ── Convert the framebuffer (cache miss) ─────────────────────
        if (!converted) {
            const auto t_conv0 = profiling::now();
            bool ok = false;
            // Run conversion inside the dedicated arena so parallel_for
            // tasks are isolated from the render thread's global arena.
            converter_arena_.execute([&] {
                switch (options_.input_format) {
                    case PipePixelFormat::YUV420P:
                        ok = convert_framebuffer_to_yuv420p(
                            *slot.fb, slot.converted.data());
                        break;
                    case PipePixelFormat::NV12:
                        ok = convert_framebuffer_to_nv12(
                            *slot.fb, slot.converted.data());
                        break;
                    case PipePixelFormat::RGBA:
                        ok = convert_framebuffer_to_rgba(
                            *slot.fb, slot.converted.data());
                        break;
                }
            });
            const auto t_conv1 = profiling::now();
            const auto conv_ms = static_cast<uint64_t>(
                profiling::duration_ms(t_conv0, t_conv1));

            if (frame_counters_) {
                frame_counters_->video_conversion_ms.fetch_add(
                    conv_ms, std::memory_order_relaxed);
                frame_counters_->frame_conversion_copy_ms.fetch_add(
                    conv_ms, std::memory_order_relaxed);
            }

            if (ok && slot.can_cache) {
                frame_cache_.insert(
                    slot.cache_key, slot.converted.data(), req_size);
            }
        }

        // Release the framebuffer memory (allow FB pool to reclaim)
        slot.fb.reset();

        // Mark as converted (state=3) and advance to next slot
        slot.state.store(3, std::memory_order_release);
        consumer_idx_.store((idx + 1) % kAsyncConversionSlots,
                            std::memory_order_relaxed);
    }
}

bool FfmpegPipeEncoder::write_next_converted_frame() {
    // Only check the slot at writer_idx_ — strict FIFO order
    size_t idx = writer_idx_.load(std::memory_order_relaxed);
    if (async_slots_[idx].state.load(std::memory_order_acquire) != 3) {
        return false;
    }

    auto& slot = async_slots_[idx];
    const size_t req_size =
        (options_.input_format == PipePixelFormat::RGBA)
            ? static_cast<size_t>(options_.width) *
              static_cast<size_t>(options_.height) * 4u
            : static_cast<size_t>(options_.width) *
              static_cast<size_t>(options_.height) * 3u / 2u;

    bool ok = write_raw_to_pipe(slot.converted.data(), req_size);

    // Mark empty and advance to next slot
    slot.state.store(0, std::memory_order_release);
    writer_idx_.store((idx + 1) % kAsyncConversionSlots,
                      std::memory_order_relaxed);
    return ok;
}

bool FfmpegPipeEncoder::write_raw_to_pipe(const uint8_t* data, size_t size) {
    if (!pipe_ || pipe_failed_) {
        return false;
    }

    const auto t_write0 = profiling::now();
    const size_t res = std::fwrite(data, 1, size, pipe_);
    const auto t_write1 = profiling::now();
    const auto write_ms = profiling::duration_ms(t_write0, t_write1);

    if (res != size) {
        pipe_failed_ = true;
        return false;
    }

    bytes_written_ += res;
    total_write_blocked_ms_ += write_ms;

    if (frame_counters_) {
        frame_counters_->video_pipe_write_ms.fetch_add(
            static_cast<uint64_t>(write_ms), std::memory_order_relaxed);
    }

    last_frame_telemetry_ = {
        .conversion_copy_ms = 0.0,
        .encoder_ms = write_ms,
        .pipe_write_ms = write_ms,
    };
    ++frames_written_;
    return true;
}

} // namespace chronon3d::cli
