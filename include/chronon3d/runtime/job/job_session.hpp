#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/runtime/context/runtime_context.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>

namespace chronon3d::runtime {

struct JobSessionConfig {
    std::shared_ptr<const compiler::CompiledProgram> program;
    Frame first_frame{0};
    Frame end_frame{0};
    std::uint64_t job_id{0};
};

/// Job-lifetime state. The runtime context is borrowed; the compiled program,
/// cancellation state, and generic RenderSession belong to this job.
class JobSession final {
public:
    JobSession(RuntimeContext& runtime, JobSessionConfig config);
    ~JobSession() = default;

    JobSession(const JobSession&) = delete;
    JobSession& operator=(const JobSession&) = delete;
    JobSession(JobSession&&) = delete;
    JobSession& operator=(JobSession&&) = delete;

    [[nodiscard]] RuntimeContext& runtime() const noexcept { return runtime_; }
    [[nodiscard]] const compiler::CompiledProgram& program() const noexcept {
        return *program_;
    }
    [[nodiscard]] const std::shared_ptr<const compiler::CompiledProgram>&
    program_ptr() const noexcept { return program_; }
    [[nodiscard]] Frame first_frame() const noexcept { return first_frame_; }
    [[nodiscard]] Frame end_frame() const noexcept { return end_frame_; }
    [[nodiscard]] std::uint64_t job_id() const noexcept { return job_id_; }

    [[nodiscard]] CancellationToken& cancellation() noexcept {
        return cancellation_;
    }
    [[nodiscard]] const CancellationToken& cancellation() const noexcept {
        return cancellation_;
    }

    [[nodiscard]] RenderSession& render_session() noexcept {
        return render_session_;
    }
    [[nodiscard]] const RenderSession& render_session() const noexcept {
        return render_session_;
    }

private:
    RuntimeContext& runtime_;
    std::shared_ptr<const compiler::CompiledProgram> program_;
    Frame first_frame_{0};
    Frame end_frame_{0};
    std::uint64_t job_id_{0};
    CancellationToken cancellation_;
    RenderSession render_session_;
};

}  // namespace chronon3d::runtime
