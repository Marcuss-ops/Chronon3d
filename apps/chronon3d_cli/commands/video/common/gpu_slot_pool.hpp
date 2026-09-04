#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/runtime/frame/frame_execution_slot.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace chronon3d::cli {

using runtime::FrameExecutionSlot;
using runtime::FrameSlotId;
using runtime::FrameSlotState;
using runtime::GpuCompletion;
using runtime::GpuCompletionTracker;
using runtime::InteropFrameState;
using runtime::FrameSlotPool;

/// CLI-owned GPU frame-slot pool for the native video export pipeline.
///
/// Composition of the canonical runtime/frame authorities — no fourth
/// engine (FrameExecutionSlotRing demolition, see AGENTS.md no-duplication
/// invariants):
///   - FrameSlotPool: fixed slot storage and CPU lifecycle state.
///   - GpuCompletionTracker: encoder completion tokens + interop state cells.
/// (Frame handoff to the writer thread stays in BoundedChannel; no second
/// queue primitive is introduced here.)
///
/// Lease discipline (B4/B7 closure):
///   - Every state change is a CAS transition from FrameSlotPool; an aborted
///     or released slot can never be resurrected by a stale producer.
///   - A slot handed to the encoder stays pinned (Encoding) until either the
///     encoder reports completion (fast path: release) or a GpuCompletion is
///     registered (slow path: retire + async reap). No error path recycles a
///     slot whose surface the encoder may still reference.
class GpuSlotPool {
public:
    static constexpr std::size_t kDefaultCapacity = 3;

    class Lease {
    public:
        Lease() noexcept = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();

        [[nodiscard]] bool valid() const noexcept {
            return pool_ != nullptr && slot_ != nullptr;
        }
        [[nodiscard]] FrameExecutionSlot& slot() noexcept { return *slot_; }
        [[nodiscard]] const FrameExecutionSlot& slot() const noexcept { return *slot_; }

        /// Fast path: encoder consumed the surface synchronously; recycle now.
        void release() noexcept;

        /// Slow path: encoder may still reference the surface; pin the slot
        /// under the given completion token until it reports ready. B7.
        void retire(std::shared_ptr<GpuCompletion> completion) noexcept;

    private:
        friend class GpuSlotPool;
        Lease(GpuSlotPool* pool, FrameExecutionSlot* slot) noexcept
            : pool_(pool), slot_(slot) {}

        GpuSlotPool* pool_{nullptr};
        FrameExecutionSlot* slot_{nullptr};
    };

    explicit GpuSlotPool(std::size_t capacity = kDefaultCapacity)
        : gpu_completions_(capacity),
          slots_(capacity, gpu_completions_) {}

    ~GpuSlotPool() { close(); }

    GpuSlotPool(const GpuSlotPool&) = delete;
    GpuSlotPool& operator=(const GpuSlotPool&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.capacity(); }

    /// Producer path. Blocks (1 ms CV poll, cancellation-aware) until a slot
    /// is free or the pool is closed / token cancelled. On close, slots whose
    /// registered completions are ready are reaped before returning nullptr.
    [[nodiscard]] Lease acquire(const CancellationToken* token = nullptr) {
        std::unique_lock lock(mutex_);
        const auto wait_start = std::chrono::steady_clock::now();
        for (;;) {
            reap_ready_locked();
            if (closed_ || (token && token->is_cancelled())) return {};
            if (auto* slot = slots_.try_acquire(FrameSlotState::Encoding)) {
                if (auto waited_us = elapsed_us(wait_start); waited_us != 0) {
                    wait_count_.fetch_add(1, std::memory_order_relaxed);
                    wait_us_.fetch_add(waited_us, std::memory_order_relaxed);
                }
                return Lease(this, slot);
            }
            cv_free_.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    /// Writer-side drain: wait until every acquired slot has been released or
    /// reaped (completions ready) or the pool is closed. Used on failure paths
    /// so a closing job never destroys surfaces the encoder still references.
    void drain() {
        std::unique_lock lock(mutex_);
        while (slots_.busy_count() > 0) {
            reap_ready_locked();
            if (closed_) return;
            cv_free_.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    void close() noexcept {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_free_.notify_all();
    }

    [[nodiscard]] std::size_t busy_count() const noexcept {
        return slots_.busy_count();
    }
    [[nodiscard]] std::uint64_t wait_count() const noexcept {
        return wait_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t wait_us() const noexcept {
        return wait_us_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] FrameExecutionSlot& slot(std::size_t index) {
        return slots_.slot(index);
    }
    [[nodiscard]] const FrameExecutionSlot& slot(std::size_t index) const {
        return slots_.slot(index);
    }

private:
    static std::uint64_t elapsed_us(std::chrono::steady_clock::time_point start) noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count());
    }

    /// Reap slots whose encoder completion fired. Runs under mutex_. B7: the
    /// slot only becomes Free here — the one path that owns the transition.
    void reap_ready_locked() noexcept {
        for (FrameSlotId slot_id = 0; slot_id < slots_.capacity(); ++slot_id) {
            if (!gpu_completions_.has_completion(slot_id) ||
                !gpu_completions_.completion_ready(slot_id)) {
                continue;
            }
            auto& completed = slots_.slot(slot_id);
            // Only retire through the interop protocol; the completion proves
            // EncodeConsumed semantics. Then recycle GPU state and release.
            (void)completed.transition_interop_state(InteropFrameState::Recyclable);
            gpu_completions_.recycle(slot_id);
            completed.native_surface_ptr = 0;
            completed.gpu_ready_sync = 0;
            slots_.release(completed);
        }
        cv_free_.notify_all();
    }

    GpuCompletionTracker gpu_completions_;
    FrameSlotPool slots_;
    mutable std::mutex mutex_;
    std::condition_variable cv_free_;
    std::atomic<std::uint64_t> wait_count_{0};
    std::atomic<std::uint64_t> wait_us_{0};
    bool closed_{false};
};

inline GpuSlotPool::Lease::Lease(Lease&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      slot_(std::exchange(other.slot_, nullptr)) {}

inline GpuSlotPool::Lease& GpuSlotPool::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::exchange(other.pool_, nullptr);
        slot_ = std::exchange(other.slot_, nullptr);
    }
    return *this;
}

inline GpuSlotPool::Lease::~Lease() {
    release();
}

inline void GpuSlotPool::Lease::release() noexcept {
    if (!pool_ || !slot_) return;
    {
        std::lock_guard lock(pool_->mutex_);
        // CAS transition: an already-released slot is left untouched. No
        // resurrection of Free slots (B4 semantics without the old ring).
        (void)pool_->slots_.transition(
            *slot_, FrameSlotState::Encoding, FrameSlotState::Free);
        pool_->gpu_completions_.recycle(slot_->slot_id);
        slot_->native_surface_ptr = 0;
        slot_->gpu_ready_sync = 0;
        pool_->slots_.release(*slot_);
    }
    pool_->cv_free_.notify_all();
    pool_ = nullptr;
    slot_ = nullptr;
}

inline void GpuSlotPool::Lease::retire(
    std::shared_ptr<GpuCompletion> completion) noexcept {
    if (!pool_ || !slot_) return;
    {
        std::lock_guard lock(pool_->mutex_);
        // The slot stays Encoding (pinned) with a registered completion; the
        // reaper transitions it to Free only when the encoder reports ready.
        pool_->gpu_completions_.retire(slot_->slot_id, std::move(completion));
    }
    pool_->cv_free_.notify_all();
    pool_ = nullptr;
    slot_ = nullptr;
}

} // namespace chronon3d::cli

namespace chronon3d::runtime {
using GpuSlotPool = ::chronon3d::cli::GpuSlotPool;
} // namespace chronon3d::runtime
