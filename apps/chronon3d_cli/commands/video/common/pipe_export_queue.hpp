#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

namespace chronon3d::cli {

// ── RenderFrameQueue — bounded blocking queue replacing moodycamel::ConcurrentQueue ─
// Wraps std::queue + std::mutex + condition_variables.  Exposes blocking
// push/pop for the video pipeline and non-blocking try_dequeue/enqueue for
// tests and legacy callers.

template <typename T>
class RenderFrameQueue {
public:
    explicit RenderFrameQueue(size_t capacity = 0)
        : capacity_(capacity) {}

    bool try_dequeue(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    void enqueue(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        not_empty_.notify_one();
    }

    size_t size_approx() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /// Blocking push.  Returns false if the queue is closed or the token is
    /// cancelled before the item can be enqueued.  On success the item is
    /// moved into the queue; on failure the caller retains ownership.
    bool push(T& item, const CancellationToken* token = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (capacity_ > 0) {
            not_full_.wait(lock, [this, token]() {
                if (token && token->is_cancelled()) return true;
                return closed_ || queue_.size() < capacity_;
            });
        }
        if (closed_) return false;
        if (token && token->is_cancelled()) return false;
        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    /// Blocking pop.  Returns false when the queue is closed and empty, or
    /// when the token is cancelled.
    bool pop(T& item, const CancellationToken* token = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this, token]() {
            if (token && token->is_cancelled()) return true;
            return closed_ || !queue_.empty();
        });
        if (closed_ && queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    /// Close the queue, waking all blocked producers and consumers.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::queue<T> queue_;
    size_t capacity_{0};
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    bool closed_{false};
};

// ── Shared frame package ────────────────────────────────────────────────────

struct RenderFramePackage {
    Frame frame_number{0};
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<FramebufferArena> arena;
};

} // namespace chronon3d::cli
