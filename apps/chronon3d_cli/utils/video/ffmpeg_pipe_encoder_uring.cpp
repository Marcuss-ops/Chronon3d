#include "ffmpeg_pipe_encoder.hpp"

#include <atomic>
#include <cstring>

#ifdef __linux__
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/io_uring.h>
#endif

namespace chronon3d::cli {

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
    for (size_t i = 0; i < kRingEntries; ++i) {
        ring_buffers_[i].resize(ring_buffer_size_);
    }
    ring_buffer_pending_.assign(kRingEntries, false);
    ring_buffer_bytes_written_.assign(kRingEntries, 0);
    ring_buffer_index_ = 0;
    pending_writes_count_ = 0;

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
                if (res == -EINTR) {
                    res = 0;
                } else {
                    ring_buffer_pending_[buf_idx] = false;
                    head++;
                    continue;
                }
            }

            if (res == 0) {
                ring_buffer_pending_[buf_idx] = false;
                head++;
                continue;
            }

            size_t written = static_cast<size_t>(res);
            ring_buffer_bytes_written_[buf_idx] += written;

            if (ring_buffer_bytes_written_[buf_idx] < ring_buffer_size_) {
                write_uring(buf_idx, ring_buffer_size_);
            } else {
                ring_buffer_pending_[buf_idx] = false;
            }
        }
        head++;
    }

    std::atomic_thread_fence(std::memory_order_release);
    *cq_head_ = head;
}

bool FfmpegPipeEncoder::write_uring(size_t buf_idx, size_t size) {
    size_t offset = ring_buffer_bytes_written_[buf_idx];
    size_t remaining = size - offset;

    unsigned sq_tail = *sq_tail_;
    unsigned index = sq_tail & sq_mask_;
    struct io_uring_sqe* sqe = &static_cast<struct io_uring_sqe*>(sqes_)[index];
    std::memset(sqe, 0, sizeof(*sqe));

    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = fileno(pipe_);
    sqe->addr = reinterpret_cast<uint64_t>(ring_buffers_[buf_idx].data() + offset);
    sqe->len = remaining;
    sqe->off = 0;
    sqe->user_data = buf_idx;

    sq_array_[index] = index;
    *sq_tail_ = sq_tail + 1;
    pending_writes_count_++;

    std::atomic_thread_fence(std::memory_order_release);

    int submitted = syscall(__NR_io_uring_enter, ring_fd_, 1, 0, 0, nullptr);
    if (submitted < 0) {
        return false;
    }

    bytes_written_ += remaining;
    return true;
}

#endif

} // namespace chronon3d::cli
