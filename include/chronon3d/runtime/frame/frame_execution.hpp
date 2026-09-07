#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/runtime/frame/frame_execution_slot.hpp>

namespace chronon3d::runtime {

class JobSession;

/// Frame-lifetime execution scope.
///
/// The object borrows JobSession state and never outlives the frame loop
/// iteration. Runtime and job ownership stay explicit in the type graph.
class FrameExecution final {
public:
    FrameExecution(JobSession& job, Frame frame, SampleTime time,
                   FrameExecutionSlot* slot = nullptr) noexcept;
    ~FrameExecution();

    FrameExecution(const FrameExecution&) = delete;
    FrameExecution& operator=(const FrameExecution&) = delete;
    FrameExecution(FrameExecution&&) = delete;
    FrameExecution& operator=(FrameExecution&&) = delete;

    [[nodiscard]] JobSession& job() const noexcept { return job_; }
    [[nodiscard]] Frame frame() const noexcept { return frame_; }
    [[nodiscard]] const SampleTime& time() const noexcept { return time_; }
    [[nodiscard]] FrameExecutionSlot* slot() const noexcept { return slot_; }
    [[nodiscard]] bool cancelled() const noexcept;

    /// Mark the frame complete. Safe to call more than once.
    void complete() noexcept;

private:
    JobSession& job_;
    Frame frame_{0};
    SampleTime time_{};
    FrameExecutionSlot* slot_{nullptr};
    bool completed_{false};
};

}  // namespace chronon3d::runtime
