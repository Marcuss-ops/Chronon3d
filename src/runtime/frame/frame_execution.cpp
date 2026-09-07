#include <chronon3d/runtime/frame/frame_execution.hpp>

#include <chronon3d/runtime/job/job_session.hpp>

namespace chronon3d::runtime {

FrameExecution::FrameExecution(JobSession& job, Frame frame, SampleTime time,
                               FrameExecutionSlot* slot,
                               FrameSlotPayload* payload) noexcept
    : job_(job), frame_(frame), time_(time), slot_(slot), payload_(payload) {
    job_.reset_frame_counters();
    ++job_.counters().frames_started;
    if (slot_) {
        slot_->frame_index = static_cast<std::uint64_t>(frame_.integral());
    }
}

FrameExecution::~FrameExecution() {
    complete();
}

bool FrameExecution::cancelled() const noexcept {
    return job_.cancellation().is_cancelled();
}

void FrameExecution::complete() noexcept {
    if (completed_) return;
    if (failed_) {
        ++job_.counters().frames_failed;
    } else {
        ++job_.counters().frames_completed;
    }
    completed_ = true;
}

}  // namespace chronon3d::runtime
