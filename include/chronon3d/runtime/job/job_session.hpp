#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/frame_history.hpp>
#include <chronon3d/runtime/context/runtime_context.hpp>
#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace chronon3d::runtime {

struct JobSessionConfig {
    std::shared_ptr<const compiler::CompiledProgram> program;
    Frame first_frame{0};
    Frame end_frame{0};
    std::uint64_t job_id{0};
};

/// Counters promoted from a frame after the frame lease is handed off.
/// The first migration keeps the aggregate intentionally small; individual
/// backend counters remain owned by their existing process/runtime authority.
struct JobCounters {
    std::uint64_t frames_started{0};
    std::uint64_t frames_completed{0};
    std::uint64_t frames_failed{0};
};

/// Job-lifetime state. The runtime context is borrowed.  History and pipeline
/// authorities belong to this job; frame-local state is leased separately by
/// FrameExecution.
class JobSession final {
public:
    static constexpr std::size_t kPipelineDepth = 3;

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

    [[nodiscard]] FrameHistory& frame_history() noexcept { return frame_history_; }
    [[nodiscard]] const FrameHistory& frame_history() const noexcept { return frame_history_; }
    [[nodiscard]] DirtyHistory& dirty_history() noexcept { return dirty_history_; }
    [[nodiscard]] const DirtyHistory& dirty_history() const noexcept { return dirty_history_; }

    [[nodiscard]] JobCounters& counters() noexcept { return counters_; }
    [[nodiscard]] const JobCounters& counters() const noexcept { return counters_; }

    [[nodiscard]] GpuCompletionTracker& gpu_completion() noexcept { return gpu_completion_; }
    [[nodiscard]] FrameSlotPool& slots() noexcept { return slots_; }
    [[nodiscard]] FrameQueue& evaluated_queue() noexcept { return evaluated_queue_; }
    [[nodiscard]] FrameQueue& rendered_queue() noexcept { return rendered_queue_; }

    void reset_temporal_history() noexcept {
        frame_history_ = FrameHistory{};
        dirty_history_.previous_layers.clear();
    }

    void reset_frame_counters() noexcept {
        dirty_history_.reset_telemetry_counters();
    }

private:
    RuntimeContext& runtime_;
    std::shared_ptr<const compiler::CompiledProgram> program_;
    Frame first_frame_{0};
    Frame end_frame_{0};
    std::uint64_t job_id_{0};
    CancellationToken cancellation_;
    FrameHistory frame_history_{};
    DirtyHistory dirty_history_{};
    JobCounters counters_{};
    GpuCompletionTracker gpu_completion_{kPipelineDepth};
    FrameSlotPool slots_{kPipelineDepth, gpu_completion_};
    FrameQueue evaluated_queue_{kPipelineDepth};
    FrameQueue rendered_queue_{kPipelineDepth};
};

}  // namespace chronon3d::runtime
