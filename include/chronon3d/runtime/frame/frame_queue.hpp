#pragma once

#include <chronon3d/runtime/frame/frame_execution_slot.hpp>

#include <cstddef>
#include <tbb/concurrent_queue.h>

namespace chronon3d::runtime {

/// Bounded handoff queue carrying slot identities only. Frame payloads remain
/// in FrameSlotPool; TBB provides the already-project-standard concurrency
/// primitive without introducing another queue dependency.
class FrameQueue {
public:
    explicit FrameQueue(std::size_t capacity);

    [[nodiscard]] bool try_push(FrameSlotId slot_id) noexcept;
    [[nodiscard]] bool try_pop(FrameSlotId& slot_id) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;

private:
    tbb::concurrent_bounded_queue<FrameSlotId> queue_;
};

} // namespace chronon3d::runtime
