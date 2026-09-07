#include <chronon3d/runtime/job/job_session.hpp>

#include <utility>

namespace chronon3d::runtime {

JobSession::JobSession(RuntimeContext& runtime, JobSessionConfig config)
    : runtime_(runtime),
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

}  // namespace chronon3d::runtime
