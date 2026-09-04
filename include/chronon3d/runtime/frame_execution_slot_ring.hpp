#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/runtime/frame/frame_execution_slot.hpp>
#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace chronon3d::runtime {

/// Compatibility facade over three single-responsibility components:
/// FrameSlotPool (fixed slot ownership), GpuCompletionTracker (native GPU
/// protocol/completions), and FrameQueue (bounded slot-id handoff).
class FrameExecutionSlotRing {
public:
    static constexpr std::size_t kDefaultCapacity = 16;
    static constexpr std::size_t kMaxCapacity = kMaxFrameExecutionSlots;

    class SlotLease {
    public:
        SlotLease() noexcept = default;
        SlotLease(const SlotLease&) = delete;
        SlotLease& operator=(const SlotLease&) = delete;
        SlotLease(SlotLease&& other) noexcept;
        SlotLease& operator=(SlotLease&& other) noexcept;
        ~SlotLease();

        [[nodiscard]] bool valid() const noexcept {
            return ring_ != nullptr && slot_ != nullptr;
        }
        [[nodiscard]] FrameExecutionSlot& slot() noexcept { return *slot_; }
        [[nodiscard]] const FrameExecutionSlot& slot() const noexcept { return *slot_; }

        void mark_ready() noexcept;
        void retire(std::shared_ptr<GpuCompletion> completion) noexcept;
        void release() noexcept;

    private:
        friend class FrameExecutionSlotRing;
        SlotLease(FrameExecutionSlotRing* ring, FrameExecutionSlot* slot) noexcept
            : ring_(ring), slot_(slot) {}

        FrameExecutionSlotRing* ring_{nullptr};
        FrameExecutionSlot* slot_{nullptr};
    };

    explicit FrameExecutionSlotRing(std::size_t capacity = kDefaultCapacity);

    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

    [[nodiscard]] FrameExecutionSlot* acquire_free_slot(
        const CancellationToken* token = nullptr);
    [[nodiscard]] SlotLease acquire_lease(const CancellationToken* token = nullptr);

    [[nodiscard]] FrameExecutionSlot* acquire_for_evaluation() noexcept;
    [[nodiscard]] bool publish_evaluated(FrameExecutionSlot& slot) noexcept;
    [[nodiscard]] FrameExecutionSlot* acquire_for_render() noexcept;
    [[nodiscard]] bool publish_rendered(FrameExecutionSlot& slot) noexcept;
    [[nodiscard]] FrameExecutionSlot* acquire_for_encoding() noexcept;
    [[nodiscard]] bool begin_encoding(FrameExecutionSlot& slot) noexcept;
    [[nodiscard]] bool release_encoded(FrameExecutionSlot& slot) noexcept;
    [[nodiscard]] bool abort(FrameExecutionSlot& slot) noexcept;

    void mark_ready(FrameExecutionSlot* slot) noexcept;
    void release_slot(FrameExecutionSlot* slot) noexcept;
    void close() noexcept;

    [[nodiscard]] std::size_t busy_count() const noexcept;
    [[nodiscard]] std::uint64_t wait_count() const noexcept;
    [[nodiscard]] std::uint64_t wait_us() const noexcept;
    [[nodiscard]] std::size_t in_flight() const noexcept;
    [[nodiscard]] std::size_t rendered_depth() const noexcept;

    void reset() noexcept;
    void retire_slot(
        FrameExecutionSlot* slot,
        std::shared_ptr<GpuCompletion> completion) noexcept;

    [[nodiscard]] FrameExecutionSlot& slot(std::size_t index);
    [[nodiscard]] const FrameExecutionSlot& slot(std::size_t index) const;

private:
    void reap_ready_completions_locked() noexcept;

    GpuCompletionTracker gpu_completions_;
    FrameSlotPool slots_;
    FrameQueue evaluated_;
    FrameQueue rendered_;

    mutable std::mutex mutex_;
    std::condition_variable cv_free_;
    std::atomic<std::uint64_t> wait_count_{0};
    std::atomic<std::uint64_t> wait_us_{0};
    bool closed_{false};
};

} // namespace chronon3d::runtime
