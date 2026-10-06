#pragma once

#include <chronon3d/runtime/frame/frame_execution_slot.hpp>
#include <chronon3d/runtime/frame/frame_slot_payload.hpp>

#include <cstddef>
#include <array>
#include <memory>
#include <atomic>
#include <vector>

namespace chronon3d::runtime {

class GpuCompletionTracker;

/// Owns the fixed slot storage and the CPU-side pipeline lifecycle only.
/// ADR-029: this is the sole writer authority for FrameSlotState.
/// No GPU completion tokens and no producer/consumer queues live here.
class FrameSlotPool {
public:
    FrameSlotPool(std::size_t capacity, GpuCompletionTracker& gpu_tracker);
    FrameSlotPool(std::size_t capacity, GpuCompletionTracker& gpu_tracker,
                  std::size_t arena_capacity_bytes);

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] FrameExecutionSlot* try_acquire(FrameSlotState acquired_state) noexcept;
    [[nodiscard]] bool transition(
        FrameExecutionSlot& slot,
        FrameSlotState expected,
        FrameSlotState next) noexcept;
    void release(FrameExecutionSlot& slot) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::size_t busy_count() const noexcept;
    /// Successful slot leases are fixed-storage pool hot-path events. This
    /// counter makes them observable without misclassifying a lease as heap
    /// allocation.
    [[nodiscard]] std::uint64_t hot_path_allocation_events() const noexcept {
        return hot_path_allocation_events_.load(std::memory_order_relaxed);
    }
    void reset_hot_path_counters() noexcept {
        hot_path_allocation_events_.store(0, std::memory_order_relaxed);
    }
    [[nodiscard]] FrameExecutionSlot& slot(FrameSlotId slot_id);
    [[nodiscard]] const FrameExecutionSlot& slot(FrameSlotId slot_id) const;
    [[nodiscard]] FrameSlotPayload& payload(FrameSlotId slot_id);
    [[nodiscard]] const FrameSlotPayload& payload(FrameSlotId slot_id) const;

private:
    std::size_t capacity_{0};
    std::size_t producer_cursor_{0};
    // Slot ids remain bounded and pointer-stable, but payload storage is
    // sized to the actual pipeline depth.  Keeping a 256-element array here
    // eagerly constructs 256 strict 4 MiB arenas (~1 GiB) for the normal
    // three-slot pipeline.
    std::array<FrameExecutionSlot, kMaxFrameExecutionSlots> slots_{};
    std::vector<std::unique_ptr<FrameSlotPayload>> payloads_{};
    std::atomic<std::uint64_t> hot_path_allocation_events_{0};
};

} // namespace chronon3d::runtime
