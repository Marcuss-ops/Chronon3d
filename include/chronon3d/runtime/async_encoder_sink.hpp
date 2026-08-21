#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <chronon3d/runtime/frame_execution_slot_ring.hpp>

namespace chronon3d::runtime {

/// Work item dispatched to the background async encoder worker.
struct AsyncEncodeTask {
    FrameExecutionSlot* slot{nullptr};
    std::uint64_t frame_index{0};
    bool is_flush{false};
};

/// Generic asynchronous encoder & muxer sink that decouples frame rendering
/// from hardware encoding and bitstream muxing.
class AsyncEncoderSink {
public:
    using EncodeDrainCallback = std::function<void(FrameExecutionSlot* slot, std::uint64_t frame_idx, bool is_flush)>;

    explicit AsyncEncoderSink(EncodeDrainCallback drain_cb)
        : m_drain_cb(std::move(drain_cb)),
          m_worker(&AsyncEncoderSink::worker_loop, this) {}

    ~AsyncEncoderSink() {
        flush_and_join();
    }

    AsyncEncoderSink(const AsyncEncoderSink&) = delete;
    AsyncEncoderSink& operator=(const AsyncEncoderSink&) = delete;

    /// Enqueue a rendered frame slot for encoding.
    void submit_frame(FrameExecutionSlot* slot, std::uint64_t frame_idx) {
        {
            std::lock_guard lock(m_mutex);
            m_queue.push(AsyncEncodeTask{slot, frame_idx, false});
            if (m_queue.size() > m_high_watermark) {
                m_high_watermark = m_queue.size();
            }
        }
        m_cv.notify_one();
    }

    /// Complete all queued tasks and terminate worker thread.
    void flush_and_join() {
        if (!m_joined.exchange(true)) {
            {
                std::lock_guard lock(m_mutex);
                m_queue.push(AsyncEncodeTask{nullptr, 0, true});
            }
            m_cv.notify_one();
            if (m_worker.joinable()) {
                m_worker.join();
            }
        }
    }

    [[nodiscard]] std::size_t queue_high_watermark() const noexcept {
        return m_high_watermark;
    }

private:
    void worker_loop() {
        while (true) {
            AsyncEncodeTask task{};
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this]() { return !m_queue.empty(); });
                task = m_queue.front();
                m_queue.pop();
            }

            if (m_drain_cb) {
                m_drain_cb(task.slot, task.frame_index, task.is_flush);
            }

            if (task.is_flush) {
                break;
            }
        }
    }

    EncodeDrainCallback m_drain_cb;
    std::queue<AsyncEncodeTask> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_joined{false};
    std::size_t m_high_watermark{0};
    std::thread m_worker;
};

} // namespace chronon3d::runtime
