#pragma once

#include <chronon3d/runtime/frame/frame_execution_slot.hpp>

#include <array>
#include <cstddef>

namespace chronon3d::runtime {

class GpuCompletionTracker;

/// Owns the fixed slot storage and the CPU-side pipeline lifecycle only.
/// No GPU completion tokens and no producer/consumer queues live here.
class FrameSlotPool {
public:
    FrameSlotPool(std::size_t capacity, GpuCompletionTracker& gpu_tracker);

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] FrameExecutionSlot* try_acquire(FrameSlotState acquired_state) noexcept;
    [[nodiscard]] bool transition(
        FrameExecutionSlot& slot,
        FrameSlotState expected,
        FrameSlotState next) noexcept;
    void set_state(FrameExecutionSlot& slot, FrameSlotState next) noexcept;
    void release(FrameExecutionSlot& slot) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::size_t busy_count() const noexcept;
    [[nodiscard]] FrameExecutionSlot& slot(FrameSlotId slot_id);
    [[nodiscard]] const FrameExecutionSlot& slot(FrameSlotId slot_id) const;

private:
    std::size_t capacity_{0};
    std::size_t producer_cursor_{0};
    std::array<FrameExecutionSlot, kMaxFrameExecutionSlots> slots_{};
};

} // namespace chronon3d::runtime
