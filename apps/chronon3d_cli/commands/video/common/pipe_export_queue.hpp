#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include "../../../utils/video/direct_yuv_frame.hpp"
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <condition_variable>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>

namespace chronon3d::cli {

/// Bounded ownership ring for GPU encode surfaces. A slot is acquired by the
/// render thread before a frame package is published and released by the
/// writer after the encoder has consumed that surface.
class FrameInteropRing {
public:
    // Six slots keep decode, composition, and NVENC ownership overlapped
    // without allowing an unbounded surface queue to grow.
    static constexpr std::size_t kSlotCount = 6;
    static constexpr std::size_t kInvalidSlot = kSlotCount;

    explicit FrameInteropRing(std::size_t slots = kSlotCount)
        : slot_count_(std::min(slots, kSlotCount)) {}

    FrameInteropRing(const FrameInteropRing&) = delete;
    FrameInteropRing& operator=(const FrameInteropRing&) = delete;

    [[nodiscard]] std::size_t acquire(const CancellationToken* token = nullptr) {
        std::unique_lock lock(mutex_);
        const auto wait_start = std::chrono::steady_clock::now();
        bool waited = false;
        for (;;) {
            if (closed_ || (token && token->is_cancelled())) return kInvalidSlot;
            for (std::size_t i = 0; i < slot_count_; ++i) {
                const auto slot = (next_slot_ + i) % slot_count_;
                if (!busy_[slot]) {
                    busy_[slot] = true;
                    next_slot_ = (slot + 1) % slot_count_;
                    if (waited) {
                        wait_count_.fetch_add(1, std::memory_order_relaxed);
                        wait_us_.fetch_add(static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - wait_start).count()),
                            std::memory_order_relaxed);
                    }
                    return slot;
                }
            }
            waited = true;
            // Poll cancellation while the writer is still consuming the
            // bounded ring. A plain wait() could strand the render thread if
            // cancellation happens without a slot release notification.
            condition_.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    void release(std::size_t slot) noexcept {
        if (slot >= slot_count_) return;
        {
            std::lock_guard lock(mutex_);
            busy_[slot] = false;
        }
        condition_.notify_one();
    }

    void close() noexcept {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    /// Number of slots currently owned by the producer/writer (GPU encode
    /// surfaces in flight).  O(slot_count) — used only for Perfetto counter
    /// tracks (frames_in_flight), guarded by tracing::TracingActive().
    [[nodiscard]] std::size_t busy_count() const noexcept {
        std::lock_guard lock(mutex_);
        std::size_t count = 0;
        for (std::size_t i = 0; i < slot_count_; ++i) {
            if (busy_[i]) ++count;
        }
        return count;
    }

    [[nodiscard]] std::uint64_t wait_count() const noexcept {
        return wait_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t wait_us() const noexcept {
        return wait_us_.load(std::memory_order_relaxed);
    }

private:
    const std::size_t slot_count_;
    std::array<bool, kSlotCount> busy_{};
    std::size_t next_slot_{0};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool closed_{false};
    std::atomic<std::uint64_t> wait_count_{0};
    std::atomic<std::uint64_t> wait_us_{0};
};

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
    graph::RenderBackend* backend{nullptr};
    runtime::RenderSurfaceRegistry* surface_registry{nullptr};
    runtime::RenderSurfaceHandle source_surface{runtime::kInvalidRenderSurfaceHandle};
    runtime::RenderSurfaceHandle native_surface{runtime::kInvalidRenderSurfaceHandle};
    std::size_t interop_slot{FrameInteropRing::kInvalidSlot};
    bool native_surface_ready{false};
    std::shared_ptr<DirectYuvFrame> direct_yuv;
};

} // namespace chronon3d::cli
