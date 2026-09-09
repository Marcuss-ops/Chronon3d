#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/runtime/bounded_spsc_ring.hpp>
#include <chronon3d/media/video/direct_yuv_frame.hpp>
#include <chronon3d/core/types/frame.hpp>
#include "gpu_slot_pool.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <variant>

namespace chronon3d::cli {

struct FullGraphFramePackage {
    Frame frame_number{0};
    runtime::GpuSlotPool::Lease slot;
    std::shared_ptr<Framebuffer> cpu_fallback;
    std::shared_ptr<FramebufferArena> cpu_arena;
};

struct DirectYuvFramePackage {
    Frame frame_number{0};
    std::shared_ptr<media::video::DirectYuvFrame> direct_yuv;
};

using RenderFramePackage = std::variant<FullGraphFramePackage, DirectYuvFramePackage>;

// ===========================================================================
// RenderFrameQueue — SPSC render→writer handoff (Wave 3)
//
// The render loop (single producer thread) hands frames to the writer thread
// (single consumer). That is an SPSC topology, so the data path is a
// fixed-capacity, lock-free BoundedSpscRing — NOT BoundedChannel's mutex +
// condition-variable + per-frame on_cancel callback registration. This is the
// architecture rule:
//
//     SPSC topology  -> BoundedSpscRing
//     MPSC/MPMC      -> BoundedChannel
//     no third queue primitive
//
// Lifecycle is an explicit three-state machine (Open/Closed/Failed), never an
// index sentinel:
//   * close() — orderly end of production (render loop finished / session
//     teardown): the consumer keeps draining until empty, then exits.
//   * fail()  — abnormal end (writer exception boundary / writer failure):
//     the producer observes a non-Open state and stops pushing; the consumer
//     still drains what was already handed off so no frame leaks an arena or
//     a slot lease.
//
// Blocking semantics are preserved WITHOUT a CV on the data path:
//   * push() blocks while the ring is full (backpressure) by retrying
//     try_push(); cancellation is checked between attempts, so no callback is
//     registered on the token per frame.
//   * pop_for() polls try_pop() until an item or a deadline, giving the GPU
//     writer the same ~1 ms interop-poll cadence BoundedChannel::pop_for had.
//
// SPSC precondition: exactly one producer thread and one consumer thread may
// touch the queue at any time. Both the FullGraph render loop and the
// DirectYUV loop are single-threaded producers; the writer thread is the
// single consumer.
// ===========================================================================
class RenderFrameQueue {
public:
    // Old PipeExportSession capacity (kArenaPoolCount). Fixed at compile time
    // because BoundedSpscRing stores the ring inline.
    static constexpr std::size_t kCapacity = 4;

    enum class State : std::uint8_t { Open = 0, Closed = 1, Failed = 2 };

    RenderFrameQueue() = default;
    RenderFrameQueue(const RenderFrameQueue&) = delete;
    RenderFrameQueue& operator=(const RenderFrameQueue&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return kCapacity;
    }

    /// Blocking push with backpressure. Returns true once the item is
    /// enqueued; false when the queue is closed/failed or the token is
    /// cancelled before the item could be enqueued (caller keeps ownership).
    bool push(RenderFramePackage& item, const CancellationToken* token = nullptr) {
        for (;;) {
            if (state_.load(std::memory_order_acquire) != State::Open) return false;
            // BoundedSpscRing::try_push takes its value by value. Calling it
            // while full would move `item` into the temporary before the
            // capacity check, so a later retry could enqueue a moved-from
            // DirectYuvFramePackage. Check fullness first; with one producer
            // only the consumer can change this state, therefore a non-full
            // observation cannot become full before try_push executes.
            if (!ring_.full() && ring_.try_push(std::move(item))) return true;
            if (token && token->is_cancelled()) return false;
            // Queue is full: backpressure. Tiny sleep so the producer does not
            // spin-burn a core while the consumer encodes the in-flight frame.
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }

    /// Non-blocking pop (single consumer). Returns false when the ring is
    /// empty.
    [[nodiscard]] bool try_pop(RenderFramePackage& item) {
        return ring_.try_pop(item);
    }

    /// Timed pop: retry try_pop() until an item arrives or `timeout` elapses.
    /// This is the writer's async-completion poll cadence — the writer checks
    /// encoder/interop completions while the producer is blocked on a full
    /// ring, exactly as the previous pop_for(1 ms) did.
    bool pop_for(RenderFramePackage& item, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (ring_.try_pop(item)) return true;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }

    /// True when the queue is no longer Open AND every handed-off item has
    /// been consumed — the writer's exit condition.
    [[nodiscard]] bool closed_and_empty() const noexcept {
        const auto s = state_.load(std::memory_order_acquire);
        return (s == State::Closed || s == State::Failed) && ring_.empty();
    }

    /// Consumer-side approximation for tracing/telemetry (SPSC: exact under
    /// the single-producer/single-consumer discipline).
    [[nodiscard]] std::size_t size_approx() const noexcept {
        return ring_.size();
    }

    /// Orderly close: consumers keep draining until empty. Idempotent.
    void close() noexcept {
        state_.store(State::Closed, std::memory_order_release);
    }

    /// Abnormal close (writer/encoder failure): producers stop pushing.
    /// Idempotent. Consumers still drain handed-off items.
    void fail() noexcept {
        state_.store(State::Failed, std::memory_order_release);
    }

    [[nodiscard]] State state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

private:
    runtime::BoundedSpscRing<RenderFramePackage, kCapacity> ring_;
    alignas(64) std::atomic<State> state_{State::Open};
};

} // namespace chronon3d::cli
