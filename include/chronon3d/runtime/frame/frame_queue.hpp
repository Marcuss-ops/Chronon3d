#pragma once

#include <chronon3d/runtime/frame/frame_execution_slot.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace chronon3d::runtime {

/// Bounded SPSC handoff queue carrying slot identities only. Frame payloads
/// remain in FrameSlotPool. The queue is fixed-capacity and allocation-free;
/// the pipeline contract guarantees one producer and one consumer.
class FrameQueue {
public:
    explicit FrameQueue(std::size_t capacity);

    [[nodiscard]] bool try_push(FrameSlotId slot_id) noexcept;
    [[nodiscard]] bool try_pop(FrameSlotId& slot_id) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;

private:
    std::array<std::optional<FrameSlotId>, kMaxFrameExecutionSlots> storage_{};
    const std::size_t capacity_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace chronon3d::runtime
