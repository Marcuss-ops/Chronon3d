#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>
#include <thread>

#if defined(_WIN32)
    #include <io.h>
#endif

#ifdef __linux__
    #include <sys/mman.h>
    #include <sys/syscall.h>
    #include <sys/uio.h>
    #include <unistd.h>
    #include <linux/io_uring.h>
    #include <atomic>
    #include <cstring>

    #ifndef __NR_io_uring_register
        #define __NR_io_uring_register 427
    #endif
    #ifndef IORING_OP_WRITE_FIXED
        #define IORING_OP_WRITE_FIXED 2
    #endif
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

    const auto t_conv0 = std::chrono::high_resolution_clock::now();
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
                ok = convert_framebuffer_to_yuv420p(fb, nullptr);
                target_buffer = yuv_buffer_.data();
                bytes_to_write = yuv_buffer_.size();
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
                ok = convert_framebuffer_to_nv12(fb, nullptr);
                target_buffer = yuv_buffer_.data();
                bytes_to_write = yuv_buffer_.size();
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
                ok = convert_framebuffer_to_rgba(fb, nullptr);
                target_buffer = rgba_buffer_.data();
                bytes_to_write = rgba_buffer_.size();
            }
            break;
        }
        default:
            return false;
    }
    const auto t_conv1 = std::chrono::high_resolution_clock::now();
    if (profiling::g_current_counters) {
        profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(
            static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count()),
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

#ifdef __linux__
bool FfmpegPipeEncoder::init_uring() {
    struct io_uring_params p;
    std::memset(&p, 0, sizeof(p));

    ring_fd_ = syscall(__NR_io_uring_setup, kRingEntries, &p);
    if (ring_fd_ < 0) {
        return false;
    }

    sq_mmap_len_ = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    cq_mmap_len_ = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    sqes_mmap_len_ = p.sq_entries * sizeof(struct io_uring_sqe);

    void* sq_ptr = mmap(0, sq_mmap_len_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        cleanup_uring();
        return false;
    }
    sq_mmap_ = sq_ptr;

    void* cq_ptr = mmap(0, cq_mmap_len_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED) {
        cleanup_uring();
        return false;
    }
    cq_mmap_ = cq_ptr;

    void* sqes_ptr = mmap(0, sqes_mmap_len_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_SQES);
    if (sqes_ptr == MAP_FAILED) {
        cleanup_uring();
        return false;
    }
    sqes_mmap_ = sqes_ptr;

    unsigned char* sq_bytes = static_cast<unsigned char*>(sq_mmap_);
    sq_head_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.head);
    sq_tail_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.tail);
    sq_ring_mask_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.ring_mask);
    sq_ring_entries_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.ring_entries);
    sq_flags_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.flags);
    sq_array_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.array);
    sq_mask_ = *sq_ring_mask_;
    sqes_ = sqes_mmap_;

    unsigned char* cq_bytes = static_cast<unsigned char*>(cq_mmap_);
    cq_head_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.head);
    cq_tail_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.tail);
    cq_ring_mask_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.ring_mask);
    cq_ring_entries_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.ring_entries);
    cqes_ = cq_bytes + p.cq_off.cqes;
    cq_mask_ = *cq_ring_mask_;

    // Allocate ring buffers pool
    ring_buffers_.resize(kRingEntries);
    std::vector<struct iovec> iov(kRingEntries);
    for (size_t i = 0; i < kRingEntries; ++i) {
        ring_buffers_[i].resize(ring_buffer_size_);
        iov[i].iov_base = ring_buffers_[i].data();
        iov[i].iov_len = ring_buffer_size_;
    }
    ring_buffer_pending_.assign(kRingEntries, false);
    ring_buffer_bytes_written_.assign(kRingEntries, 0);
    ring_buffer_index_ = 0;
    pending_writes_count_ = 0;
    buffers_registered_ = false;

    int reg_rc = syscall(__NR_io_uring_register, ring_fd_, 0 /* IORING_REGISTER_BUFFERS */, iov.data(), kRingEntries);
    if (reg_rc >= 0) {
        buffers_registered_ = true;
    }

    return true;
}

void FfmpegPipeEncoder::cleanup_uring() {
    if (sqes_mmap_) {
        munmap(sqes_mmap_, sqes_mmap_len_);
        sqes_mmap_ = nullptr;
        sqes_ = nullptr;
    }
    if (cq_mmap_) {
        munmap(cq_mmap_, cq_mmap_len_);
        cq_mmap_ = nullptr;
        cqes_ = nullptr;
    }
    if (sq_mmap_) {
        munmap(sq_mmap_, sq_mmap_len_);
        sq_mmap_ = nullptr;
    }
    if (ring_fd_ >= 0) {
        ::close(ring_fd_);
        ring_fd_ = -1;
    }
    use_uring_ = false;
}

void FfmpegPipeEncoder::reap_completed_uring(bool wait) {
    if (ring_fd_ < 0) return;

    unsigned head = *cq_head_;
    unsigned tail = *cq_tail_;
    std::array<size_t, kRingEntries> retry_bufs{};
    size_t retry_count = 0;

    if (wait && head == tail) {
        syscall(__NR_io_uring_enter, ring_fd_, 0, 1, IORING_ENTER_GETEVENTS, nullptr);
        tail = *cq_tail_;
    }

    struct io_uring_cqe* cqes = static_cast<struct io_uring_cqe*>(cqes_);
    while (head != tail) {
        struct io_uring_cqe* cqe = &cqes[head & cq_mask_];
        uint64_t buf_idx = cqe->user_data;
        int res = cqe->res;

        if (pending_writes_count_ > 0) {
            pending_writes_count_--;
        }

        if (buf_idx < kRingEntries) {
            if (res < 0) {
                if (res == -EINTR || res == -EAGAIN || res == -EWOULDBLOCK) {
                    if (retry_count < retry_bufs.size()) {
                        retry_bufs[retry_count++] = static_cast<size_t>(buf_idx);
                    }
                } else {
                    ring_buffer_pending_[buf_idx] = false;
                    pipe_failed_ = true;
                    head++;
                    continue;
                }
            }

            if (res == 0) {
                if (retry_count < retry_bufs.size()) {
                    retry_bufs[retry_count++] = static_cast<size_t>(buf_idx);
                }
                head++;
                continue;
            }

            size_t written = static_cast<size_t>(res);
            ring_buffer_bytes_written_[buf_idx] += written;

            if (ring_buffer_bytes_written_[buf_idx] < ring_buffer_size_) {
                if (retry_count < retry_bufs.size()) {
                    retry_bufs[retry_count++] = static_cast<size_t>(buf_idx);
                }
            } else {
                ring_buffer_pending_[buf_idx] = false;
            }
        }
        head++;
    }

    std::atomic_thread_fence(std::memory_order_release);
    *cq_head_ = head;

    for (size_t i = 0; i < retry_count && !pipe_failed_; ++i) {
        const size_t buf_idx = retry_bufs[i];
        if (buf_idx < kRingEntries && ring_buffer_pending_[buf_idx]) {
            if (!write_uring(buf_idx, ring_buffer_size_)) {
                break;
            }
        }
    }
}

bool FfmpegPipeEncoder::write_uring(size_t buf_idx, size_t size) {
    if (pipe_failed_ || ring_fd_ < 0) {
        return false;
    }

    size_t offset = ring_buffer_bytes_written_[buf_idx];
    size_t remaining = size - offset;
    if (remaining == 0) {
        return true;
    }

    while ((static_cast<size_t>(*sq_tail_ - *sq_head_) >= static_cast<size_t>(*sq_ring_entries_))) {
        reap_completed_uring(true);
        if (pipe_failed_) {
            return false;
        }
    }

    unsigned sq_tail = *sq_tail_;
    unsigned index = sq_tail & sq_mask_;
    struct io_uring_sqe* sqe = &static_cast<struct io_uring_sqe*>(sqes_)[index];
    std::memset(sqe, 0, sizeof(*sqe));

    if (buffers_registered_) {
        sqe->opcode = IORING_OP_WRITE_FIXED;
        sqe->buf_index = buf_idx;
    } else {
        sqe->opcode = IORING_OP_WRITE;
    }
    sqe->fd = fileno(pipe_);
    sqe->addr = reinterpret_cast<uint64_t>(ring_buffers_[buf_idx].data() + offset);
    sqe->len = remaining;
    sqe->off = 0;
    sqe->user_data = buf_idx;

    sq_array_[index] = index;
    *sq_tail_ = sq_tail + 1;

    std::atomic_thread_fence(std::memory_order_release);

    int submitted = syscall(__NR_io_uring_enter, ring_fd_, 1, 0, 0, nullptr);
    if (submitted < 0) {
        if (submitted == -EAGAIN || submitted == -EBUSY || submitted == -EINTR) {
            std::this_thread::yield();
            return false;
        }
        pipe_failed_ = true;
        return false;
    }

    pending_writes_count_++;

    bytes_written_ += remaining;
    return true;
}
#endif

} // namespace chronon3d::cli
