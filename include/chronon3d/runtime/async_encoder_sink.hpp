#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <chronon3d/runtime/bounded_channel.hpp>
#include <chronon3d/runtime/frame/frame_execution_slot.hpp>

namespace chronon3d::runtime {

/// Work item dispatched to the background async encoder worker.
struct AsyncEncodeTask {
    FrameExecutionSlot* slot{nullptr};
    std::uint64_t frame_index{0};
    bool is_flush{false};
};

enum class AsyncEncoderSinkState : std::uint8_t {
    Running,
    Draining,
    Flushed,
    Closed,
};

/// Generic asynchronous encoder & muxer sink that decouples frame rendering
/// from hardware encoding and bitstream muxing.
///
/// The queue is deliberately bounded: an offline producer blocks instead of
/// growing memory without limit or dropping frames.  Lifecycle transitions are
/// explicit so a flush marker is emitted at most once and submissions after
/// draining begins are rejected deterministically.
class AsyncEncoderSink {
public:
    using EncodeDrainCallback = std::function<void(FrameExecutionSlot* slot, std::uint64_t frame_idx, bool is_flush)>;

    static constexpr std::size_t kDefaultQueueCapacity = 4;

    explicit AsyncEncoderSink(EncodeDrainCallback drain_cb,
                              std::size_t queue_capacity = kDefaultQueueCapacity)
        : m_drain_cb(std::move(drain_cb)),
          m_queue(require_bounded_capacity(queue_capacity)),
          m_queue_capacity(queue_capacity),
          m_worker(&AsyncEncoderSink::worker_loop, this) {}

    ~AsyncEncoderSink() noexcept {
        try {
            flush_and_join();
        } catch (...) {
            // Destructors must not surface worker failures. Explicit callers of
            // flush_and_join() still receive the original exception.
        }
    }

    AsyncEncoderSink(const AsyncEncoderSink&) = delete;
    AsyncEncoderSink& operator=(const AsyncEncoderSink&) = delete;

    /// Enqueue a rendered frame slot for encoding. Blocks while the bounded
    /// queue is full. Throws if draining has started or if the worker failed.
    void submit_frame(FrameExecutionSlot* slot, std::uint64_t frame_idx) {
        std::lock_guard submission_lock(m_submission_mutex);
        rethrow_worker_failure();
        if (m_state.load(std::memory_order_acquire) != AsyncEncoderSinkState::Running) {
            throw std::logic_error("AsyncEncoderSink does not accept frames after draining begins");
        }

        AsyncEncodeTask task{slot, frame_idx, false};
        if (!m_queue.push(task)) {
            rethrow_worker_failure();
            throw std::runtime_error("AsyncEncoderSink queue closed while submitting a frame");
        }
        record_queue_depth();
        rethrow_worker_failure();
    }

    /// Complete all queued tasks and terminate the worker thread. Repeated
    /// calls are idempotent; a worker exception is rethrown after the worker is
    /// joined so blocked producers are never stranded.
    void flush_and_join() {
        std::lock_guard lifecycle_lock(m_lifecycle_mutex);
        bool queue_closed_before_flush = false;

        {
            std::lock_guard submission_lock(m_submission_mutex);
            const auto current = m_state.load(std::memory_order_acquire);
            if (current == AsyncEncoderSinkState::Running) {
                m_state.store(AsyncEncoderSinkState::Draining, std::memory_order_release);
                AsyncEncodeTask flush_task{nullptr, 0, true};
                if (!m_queue.push(flush_task)) {
                    queue_closed_before_flush = true;
                } else {
                    record_queue_depth();
                }
            }
        }

        if (m_worker.joinable()) {
            m_worker.join();
        }

        m_queue.close();
        m_state.store(AsyncEncoderSinkState::Closed, std::memory_order_release);
        rethrow_worker_failure();
        if (queue_closed_before_flush) {
            throw std::runtime_error("AsyncEncoderSink queue closed before the flush marker was accepted");
        }
    }

    [[nodiscard]] std::size_t queue_high_watermark() const noexcept {
        return m_high_watermark.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t queue_capacity() const noexcept {
        return m_queue_capacity;
    }

    [[nodiscard]] AsyncEncoderSinkState state() const noexcept {
        return m_state.load(std::memory_order_acquire);
    }

private:
    static std::size_t require_bounded_capacity(std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("AsyncEncoderSink queue capacity must be greater than zero");
        }
        return capacity;
    }

    void worker_loop() noexcept {
        try {
            AsyncEncodeTask task{};
            while (m_queue.pop(task)) {
                if (m_drain_cb) {
                    m_drain_cb(task.slot, task.frame_index, task.is_flush);
                }

                if (task.is_flush) {
                    m_state.store(AsyncEncoderSinkState::Flushed, std::memory_order_release);
                    m_queue.close();
                    return;
                }
            }
        } catch (...) {
            record_worker_failure(std::current_exception());
            m_state.store(AsyncEncoderSinkState::Closed, std::memory_order_release);
            m_queue.close();
        }
    }

    void record_worker_failure(std::exception_ptr failure) noexcept {
        std::lock_guard failure_lock(m_failure_mutex);
        if (!m_worker_failure) {
            m_worker_failure = std::move(failure);
        }
    }

    void rethrow_worker_failure() {
        std::exception_ptr failure;
        {
            std::lock_guard failure_lock(m_failure_mutex);
            failure = m_worker_failure;
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

    void record_queue_depth() noexcept {
        const auto depth = m_queue.size_approx();
        auto high = m_high_watermark.load(std::memory_order_relaxed);
        while (depth > high &&
               !m_high_watermark.compare_exchange_weak(
                   high, depth, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    EncodeDrainCallback m_drain_cb;
    BoundedChannel<AsyncEncodeTask> m_queue;
    const std::size_t m_queue_capacity;
    std::mutex m_submission_mutex;
    std::mutex m_lifecycle_mutex;
    std::mutex m_failure_mutex;
    std::exception_ptr m_worker_failure;
    std::atomic<AsyncEncoderSinkState> m_state{AsyncEncoderSinkState::Running};
    std::atomic<std::size_t> m_high_watermark{0};
    std::thread m_worker;
};

} // namespace chronon3d::runtime