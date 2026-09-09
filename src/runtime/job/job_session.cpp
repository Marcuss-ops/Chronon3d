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

JobSession::JobSession(RuntimeContext& runtime, Frame first_frame,
                       Frame end_frame, std::uint64_t job_id)
    : runtime_(&runtime),
      first_frame_(first_frame),
      end_frame_(end_frame),
      job_id_(job_id) {
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
    memory_aggregate_state->reset();
    // FIXING.md H-07 — job-lifetime peak: shrink only when the buffer has
    // grown pathologically (16 MiB ≈ 4× 1080p float scratch). Keeps warm
    // reuse for normal jobs; bounds idle memory for outlier frames.
    static constexpr std::size_t kShrinkThreshold = 16 * 1024 * 1024 / sizeof(float);
    if (download_scratch_.capacity() > kShrinkThreshold) {
        download_scratch_.clear();
        download_scratch_.shrink_to_fit();
    }
}

}  // namespace chronon3d::runtime
