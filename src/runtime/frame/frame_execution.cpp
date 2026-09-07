#include <chronon3d/runtime/frame/frame_execution.hpp>

#include <chronon3d/runtime/job/job_session.hpp>

namespace chronon3d::runtime {

FrameExecution::FrameExecution(JobSession& job, Frame frame, SampleTime time,
                               FrameExecutionSlot* slot) noexcept
    : job_(job), frame_(frame), time_(time), slot_(slot) {
    job_.render_session().reset_frame_temporaries();
    job_.render_session().prepared_composition =
        &job_.program().compiled_composition();
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
    job_.render_session().prepared_composition = nullptr;
    completed_ = true;
}

}  // namespace chronon3d::runtime
