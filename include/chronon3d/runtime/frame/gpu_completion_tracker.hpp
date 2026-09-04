#pragma once

#include <chronon3d/runtime/frame/frame_execution_slot.hpp>

#include <array>
#include <cstddef>
#include <memory>

namespace chronon3d::runtime {

/// Owns native GPU protocol state and asynchronous completion tokens for the
/// fixed frame-slot pool. It never owns frame payloads or scheduling queues.
class GpuCompletionTracker {
public:
    explicit GpuCompletionTracker(std::size_t capacity);

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] InteropFrameStateCell* state_cell(FrameSlotId slot_id) noexcept;
    [[nodiscard]] const InteropFrameStateCell* state_cell(FrameSlotId slot_id) const noexcept;
    [[nodiscard]] InteropFrameState interop_state(FrameSlotId slot_id) const noexcept;

    void retire(FrameSlotId slot_id, std::shared_ptr<GpuCompletion> completion) noexcept;
    [[nodiscard]] bool has_completion(FrameSlotId slot_id) const noexcept;
    [[nodiscard]] bool completion_ready(FrameSlotId slot_id) const noexcept;
    void recycle(FrameSlotId slot_id) noexcept;
    void reset() noexcept;

private:
    std::size_t capacity_{0};
    std::array<InteropFrameStateCell, kMaxFrameExecutionSlots> interop_states_{};
    std::array<std::shared_ptr<GpuCompletion>, kMaxFrameExecutionSlots> completions_{};
};

} // namespace chronon3d::runtime
