#pragma once

#include <chronon3d/core/cancellation_token.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>

namespace chronon3d::runtime {

/// Bounded blocking channel (mutex + condition variables).
///
/// Promoted from the CLI-specific `RenderFrameQueue` to a runtime primitive:
/// same implementation and semantics (bounded, blocking, cancellation-aware,
/// `pop_for` for asynchronous completions), no SPSC specialization yet.
/// Owns no threads and performs no allocation beyond the wrapped
/// `std::queue` growth; the capacity bound provides back-pressure between
/// pipeline stages.
template <typename T>
class BoundedChannel {
public:
    explicit BoundedChannel(size_t capacity = 0)
        : capacity_(capacity) {}

    size_t size_approx() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /// Blocking push.  Returns false if the channel is closed or the token is
    /// cancelled before the item can be enqueued.  On success the item is
    /// moved into the queue; on failure the caller retains ownership.
    bool push(T& item, const CancellationToken* token = nullptr) {
        const auto callback_id = token ? token->on_cancel([this]() {
            not_full_.notify_all();
            not_empty_.notify_all();
        }) : 0;
        std::unique_lock<std::mutex> lock(mutex_);
        if (capacity_ > 0) {
            not_full_.wait(lock, [this, token]() {
                if (token && token->is_cancelled()) return true;
                return closed_ || queue_.size() < capacity_;
            });
        }
        if (closed_) {
            if (token) token->remove_on_cancel(callback_id);
            return false;
        }
        if (token && token->is_cancelled()) {
            token->remove_on_cancel(callback_id);
            return false;
        }
        queue_.push(std::move(item));
        not_empty_.notify_one();
        if (token) token->remove_on_cancel(callback_id);
        return true;
    }

    /// Blocking pop.  Returns false when the channel is closed and empty, or
    /// when the token is cancelled.
    bool pop(T& item, const CancellationToken* token = nullptr) {
        const auto callback_id = token ? token->on_cancel([this]() {
            not_full_.notify_all();
            not_empty_.notify_all();
        }) : 0;
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this, token]() {
            if (token && token->is_cancelled()) return true;
            return closed_ || !queue_.empty();
        });
        if (closed_ && queue_.empty()) {
            if (token) token->remove_on_cancel(callback_id);
            return false;
        }
        if (token && token->is_cancelled()) {
            if (token) token->remove_on_cancel(callback_id);
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        if (token) token->remove_on_cancel(callback_id);
        return true;
    }

    /// Timed pop used by the GPU writer to poll asynchronous interop
    /// completions while the producer is blocked on a full surface ring.
    bool pop_for(T& item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_.wait_for(lock, timeout, [this]() {
                return closed_ || !queue_.empty();
            })) {
            return false;
        }
        if (closed_ && queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    [[nodiscard]] bool closed_and_empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ && queue_.empty();
    }

    /// Close the channel, waking all blocked producers and consumers.
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

} // namespace chronon3d::runtime
