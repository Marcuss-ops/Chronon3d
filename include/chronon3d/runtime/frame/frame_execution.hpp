#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/runtime/frame/frame_execution_slot.hpp>
#include <chronon3d/runtime/frame/frame_slot_payload.hpp>

#include <cstdint>

namespace chronon3d::runtime {

class JobSession;

/// Counters that belong to the active frame lease.  They are deliberately
/// plain data; promotion to JobCounters happens when the lease completes.
struct FrameCounters {
    std::uint64_t nodes_executed{0};
    std::uint64_t nodes_skipped{0};
    std::uint64_t errors{0};
};

/// Frame-lifetime execution scope.
///
/// The object borrows JobSession state and never outlives the frame loop
/// iteration. Runtime and job ownership stay explicit in the type graph.
class FrameExecution final {
public:
    /// Construct an execution over an already-leased pipeline slot.
    /// A frame execution without a slot/payload is not a production state:
    /// keeping that compatibility path made frame-local ownership optional
    /// and allowed work to bypass FrameSlotPool.
    FrameExecution(JobSession& job, Frame frame, SampleTime time,
                   FrameExecutionSlot& slot,
                   FrameSlotPayload& payload) noexcept;
    ~FrameExecution();

    FrameExecution(const FrameExecution&) = delete;
    FrameExecution& operator=(const FrameExecution&) = delete;
    FrameExecution(FrameExecution&&) = delete;
    FrameExecution& operator=(FrameExecution&&) = delete;

    [[nodiscard]] JobSession& job() const noexcept { return job_; }
    [[nodiscard]] Frame frame() const noexcept { return frame_; }
    [[nodiscard]] const SampleTime& time() const noexcept { return time_; }
    [[nodiscard]] FrameExecutionSlot* slot() const noexcept { return &slot_; }
    [[nodiscard]] FrameSlotPayload* payload() const noexcept { return &payload_; }
    [[nodiscard]] FrameArena* arena() const noexcept { return &payload_.arena; }
    [[nodiscard]] FrameCounters& counters() noexcept { return counters_; }
    [[nodiscard]] const FrameCounters& counters() const noexcept { return counters_; }
    [[nodiscard]] bool cancelled() const noexcept;

    /// Mark the active frame failed. The aggregate is updated exactly once
    /// when the frame lease is completed.
    void fail() noexcept { failed_ = true; ++counters_.errors; }

    /// Mark the frame complete. Safe to call more than once.
    void complete() noexcept;

private:
    JobSession& job_;
    Frame frame_{0};
    SampleTime time_{};
    FrameExecutionSlot& slot_;
    FrameSlotPayload& payload_;
    FrameCounters counters_{};
    bool failed_{false};
    bool completed_{false};
};

}  // namespace chronon3d::runtime
