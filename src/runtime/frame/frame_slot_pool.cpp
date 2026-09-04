#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>

#include <limits>
#include <stdexcept>

namespace chronon3d::runtime {

FrameSlotPool::FrameSlotPool(
    std::size_t capacity,
    GpuCompletionTracker& gpu_tracker)
    : capacity_(capacity) {
    if (capacity_ == 0 || capacity_ > kMaxFrameExecutionSlots ||
        gpu_tracker.capacity() != capacity_) {
        throw std::invalid_argument(
            "FrameSlotPool: invalid capacity or tracker capacity mismatch");
    }
    for (FrameSlotId slot_id = 0; slot_id < capacity_; ++slot_id) {
        auto& entry = slots_[slot_id];
        entry.slot_id = slot_id;
        entry.interop_state.bind(gpu_tracker.state_cell(slot_id));
        entry.state.store(FrameSlotState::Free, std::memory_order_relaxed);
    }
}

FrameExecutionSlot* FrameSlotPool::try_acquire(FrameSlotState acquired_state) noexcept {
    for (std::size_t offset = 0; offset < capacity_; ++offset) {
        const auto slot_id = (producer_cursor_ + offset) % capacity_;
        auto& candidate = slots_[slot_id];
        auto expected = FrameSlotState::Free;
        if (candidate.state.compare_exchange_strong(
                expected, acquired_state,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            producer_cursor_ = (slot_id + 1) % capacity_;
            return &candidate;
        }
    }
    return nullptr;
}

bool FrameSlotPool::transition(
    FrameExecutionSlot& slot,
    FrameSlotState expected,
    FrameSlotState next) noexcept {
    return slot.state.compare_exchange_strong(
        expected, next, std::memory_order_acq_rel, std::memory_order_acquire);
}

void FrameSlotPool::set_state(
    FrameExecutionSlot& slot,
    FrameSlotState next) noexcept {
    slot.state.store(next, std::memory_order_release);
}

void FrameSlotPool::release(FrameExecutionSlot& slot) noexcept {
    slot.frame_index = std::numeric_limits<std::uint64_t>::max();
    slot.state.store(FrameSlotState::Free, std::memory_order_release);
}

void FrameSlotPool::reset() noexcept {
    producer_cursor_ = 0;
    for (FrameSlotId slot_id = 0; slot_id < capacity_; ++slot_id) {
        auto& entry = slots_[slot_id];
        entry.frame_index = std::numeric_limits<std::uint64_t>::max();
        entry.native_surface_ptr = 0;
        entry.gpu_ready_sync = 0;
        entry.state.store(FrameSlotState::Free, std::memory_order_release);
    }
}

std::size_t FrameSlotPool::busy_count() const noexcept {
    std::size_t count = 0;
    for (FrameSlotId slot_id = 0; slot_id < capacity_; ++slot_id) {
        if (slots_[slot_id].state.load(std::memory_order_acquire) !=
            FrameSlotState::Free) {
            ++count;
        }
    }
    return count;
}

FrameExecutionSlot& FrameSlotPool::slot(FrameSlotId slot_id) {
    if (slot_id >= capacity_) {
        throw std::out_of_range("FrameSlotPool: slot id out of range");
    }
    return slots_[slot_id];
}

const FrameExecutionSlot& FrameSlotPool::slot(FrameSlotId slot_id) const {
    if (slot_id >= capacity_) {
        throw std::out_of_range("FrameSlotPool: slot id out of range");
    }
    return slots_[slot_id];
}

} // namespace chronon3d::runtime
