#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/context/runtime_context.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <utility>

namespace chronon3d::runtime {

void RenderErrorSlot::clear() {
    std::lock_guard lock(mutex_);
    error_.reset();
}

void RenderErrorSlot::publish_first(const graph::NodeExecutionError& error) {
    std::lock_guard lock(mutex_);
    if (!error_) error_ = std::make_shared<const graph::NodeExecutionError>(error);
}

std::shared_ptr<const graph::NodeExecutionError> RenderErrorSlot::load() const {
    std::lock_guard lock(mutex_);
    return error_;
}

JobSession::JobSession(RuntimeContext& runtime, JobSessionConfig config)
    : runtime_(&runtime),
      program_(std::move(config.program)),
      first_frame_(config.first_frame),
      end_frame_(config.end_frame),
      job_id_(config.job_id) {
    if (!program_) {
        throw std::invalid_argument("JobSession requires a CompiledProgram");
    }
    if (end_frame_ < first_frame_) {
        throw std::invalid_argument("JobSession frame range is inverted");
    }
}

graph::GraphExecutor* JobSession::executor() const noexcept {
    return executor_ ? executor_ : (runtime_ ? &runtime_->executor() : nullptr);
}

void JobSession::reset_job() {
    reset_frame_values();
    reset_temporal_history();
    reset_scene_state();
}

}  // namespace chronon3d::runtime
