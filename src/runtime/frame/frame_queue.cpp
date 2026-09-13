#include <chronon3d/runtime/frame/frame_queue.hpp>

#include <stdexcept>

namespace chronon3d::runtime {

FrameQueue::FrameQueue(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0 || capacity > kMaxFrameExecutionSlots) {
        throw std::invalid_argument(
            "FrameQueue: capacity must be in [1, kMaxFrameExecutionSlots]");
    }
}

bool FrameQueue::try_push(FrameSlotId slot_id) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= capacity_) return false;
    storage_[head % capacity_].emplace(slot_id);
    head_.store(head + 1, std::memory_order_release);
    return true;
}

bool FrameQueue::try_pop(FrameSlotId& slot_id) noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    auto& item = storage_[tail % capacity_];
    slot_id = *item;
    item.reset();
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

std::size_t FrameQueue::size() const noexcept {
    return head_.load(std::memory_order_acquire) -
        tail_.load(std::memory_order_acquire);
}

void FrameQueue::clear() noexcept {
    FrameSlotId slot_id = 0;
    while (try_pop(slot_id)) {}
}

} // namespace chronon3d::runtime
