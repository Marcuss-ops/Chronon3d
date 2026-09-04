#include <chronon3d/runtime/frame/frame_queue.hpp>

#include <stdexcept>

namespace chronon3d::runtime {

FrameQueue::FrameQueue(std::size_t capacity) {
    if (capacity == 0 || capacity > kMaxFrameExecutionSlots) {
        throw std::invalid_argument(
            "FrameQueue: capacity must be in [1, kMaxFrameExecutionSlots]");
    }
    queue_.set_capacity(static_cast<std::ptrdiff_t>(capacity));
}

bool FrameQueue::try_push(FrameSlotId slot_id) noexcept {
    return queue_.try_push(slot_id);
}

bool FrameQueue::try_pop(FrameSlotId& slot_id) noexcept {
    return queue_.try_pop(slot_id);
}

std::size_t FrameQueue::size() const noexcept {
    const auto current = queue_.size();
    return current > 0 ? static_cast<std::size_t>(current) : 0U;
}

void FrameQueue::clear() noexcept {
    FrameSlotId slot_id = 0;
    while (queue_.try_pop(slot_id)) {
    }
}

} // namespace chronon3d::runtime
