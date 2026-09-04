#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>

#include <stdexcept>
#include <utility>

namespace chronon3d::runtime {

GpuCompletionTracker::GpuCompletionTracker(std::size_t capacity)
    : capacity_(capacity) {
    if (capacity_ == 0 || capacity_ > kMaxFrameExecutionSlots) {
        throw std::invalid_argument(
            "GpuCompletionTracker: capacity must be in [1, kMaxFrameExecutionSlots]");
    }
    reset();
}

InteropFrameStateCell* GpuCompletionTracker::state_cell(FrameSlotId slot_id) noexcept {
    return slot_id < capacity_ ? &interop_states_[slot_id] : nullptr;
}

const InteropFrameStateCell* GpuCompletionTracker::state_cell(
    FrameSlotId slot_id) const noexcept {
    return slot_id < capacity_ ? &interop_states_[slot_id] : nullptr;
}

InteropFrameState GpuCompletionTracker::interop_state(FrameSlotId slot_id) const noexcept {
    const auto* cell = state_cell(slot_id);
    return cell ? cell->value.load(std::memory_order_acquire)
                : InteropFrameState::Recyclable;
}

void GpuCompletionTracker::retire(
    FrameSlotId slot_id,
    std::shared_ptr<GpuCompletion> completion) noexcept {
    if (slot_id >= capacity_) return;
    completions_[slot_id] = std::move(completion);
}

bool GpuCompletionTracker::has_completion(FrameSlotId slot_id) const noexcept {
    return slot_id < capacity_ && static_cast<bool>(completions_[slot_id]);
}

bool GpuCompletionTracker::completion_ready(FrameSlotId slot_id) const noexcept {
    if (slot_id >= capacity_) return false;
    const auto& completion = completions_[slot_id];
    return completion && completion->ready();
}

void GpuCompletionTracker::recycle(FrameSlotId slot_id) noexcept {
    if (slot_id >= capacity_) return;
    completions_[slot_id].reset();
    interop_states_[slot_id].value.store(
        InteropFrameState::Recyclable, std::memory_order_release);
}

void GpuCompletionTracker::reset() noexcept {
    for (FrameSlotId slot_id = 0; slot_id < capacity_; ++slot_id) {
        recycle(slot_id);
    }
}

} // namespace chronon3d::runtime
