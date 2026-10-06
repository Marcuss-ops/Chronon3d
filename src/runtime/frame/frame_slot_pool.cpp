#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>

#include <limits>
#include <stdexcept>

namespace chronon3d::runtime {

FrameSlotPool::FrameSlotPool(
    std::size_t capacity,
    GpuCompletionTracker& gpu_tracker)
    : FrameSlotPool(capacity, gpu_tracker, 4u * 1024u * 1024u) {}

FrameSlotPool::FrameSlotPool(
    std::size_t capacity,
    GpuCompletionTracker& gpu_tracker,
    std::size_t arena_capacity_bytes)
    : capacity_(capacity) {
    if (capacity_ == 0 || capacity_ > kMaxFrameExecutionSlots ||
        arena_capacity_bytes == 0 ||
        gpu_tracker.capacity() != capacity_) {
        throw std::invalid_argument(
            "FrameSlotPool: invalid capacity or tracker capacity mismatch");
    }
    payloads_.reserve(capacity_);
    for (std::size_t index = 0; index < capacity_; ++index) {
        payloads_.push_back(
            std::make_unique<FrameSlotPayload>(arena_capacity_bytes));
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
            hot_path_allocation_events_.fetch_add(1, std::memory_order_relaxed);
            if (profiling::g_current_counters != nullptr) {
                profiling::g_current_counters->frame_slot_pool_acquire_events.fetch_add(
                    1, std::memory_order_relaxed);
            }
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

void FrameSlotPool::release(FrameExecutionSlot& slot) noexcept {
    slot.frame_index = std::numeric_limits<std::uint64_t>::max();
    slot.state.store(FrameSlotState::Free, std::memory_order_release);
}

void FrameSlotPool::reset() noexcept {
    producer_cursor_ = 0;
    reset_hot_path_counters();
    for (FrameSlotId slot_id = 0; slot_id < capacity_; ++slot_id) {
        payloads_[slot_id]->reset();
        auto& entry = slots_[slot_id];
        entry.frame_index = std::numeric_limits<std::uint64_t>::max();
        entry.native_surface_ptr = 0;
        entry.gpu_ready_sync = 0;
        entry.state.store(FrameSlotState::Free, std::memory_order_release);
    }
}

FrameSlotPayload& FrameSlotPool::payload(FrameSlotId slot_id) {
    if (slot_id >= capacity_) {
        throw std::out_of_range("FrameSlotPool: payload id out of range");
    }
    return *payloads_[slot_id];
}

const FrameSlotPayload& FrameSlotPool::payload(FrameSlotId slot_id) const {
    if (slot_id >= capacity_) {
        throw std::out_of_range("FrameSlotPool: payload id out of range");
    }
    return *payloads_[slot_id];
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
