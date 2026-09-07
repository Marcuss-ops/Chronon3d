#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/frame_history.hpp>
#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/internal/runtime/history_state.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/executor/execution_workspace.hpp>
#include <chronon3d/text/text_run.hpp>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <mutex>

namespace chronon3d::graph {
struct NodeExecutionError;
class GraphExecutor;
}

namespace chronon3d::runtime {

class RuntimeContext;

struct FrameHistorySlot : FrameHistory {
    using FrameHistory::operator=;
    FrameHistory& operator()() noexcept { return *this; }
    const FrameHistory& operator()() const noexcept { return *this; }
};

struct DirtyHistorySlot : DirtyHistory {
    using DirtyHistory::operator=;
    DirtyHistory& operator()() noexcept { return *this; }
    const DirtyHistory& operator()() const noexcept { return *this; }
};

/// Frame error channel owned by the job and cleared at each frame boundary.
class RenderErrorSlot final {
public:
    void clear();
    void publish_first(const graph::NodeExecutionError& error);
    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError> load() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const graph::NodeExecutionError> error_;
};

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

    /// Empty compatibility session for standalone graph diagnostics. It is
    /// intentionally not a render job until a RuntimeContext is bound.
    JobSession() = default;
    explicit JobSession(RuntimeContext& runtime) noexcept : runtime_(&runtime) {}
    JobSession(RuntimeContext& runtime, JobSessionConfig config);
    ~JobSession() = default;

    JobSession(const JobSession&) = delete;
    JobSession& operator=(const JobSession&) = delete;
    JobSession(JobSession&&) noexcept = default;
    JobSession& operator=(JobSession&&) noexcept = default;

    [[nodiscard]] RuntimeContext& runtime() const noexcept { return *runtime_; }
    [[nodiscard]] bool has_runtime() const noexcept { return runtime_ != nullptr; }
    void bind_runtime(RuntimeContext& runtime) noexcept { runtime_ = &runtime; }
    void bind_executor(graph::GraphExecutor& executor) noexcept { executor_ = &executor; }
    [[nodiscard]] graph::GraphExecutor* executor() const noexcept;
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

    [[nodiscard]] JobCounters& counters() noexcept { return counters_; }
    [[nodiscard]] const JobCounters& counters() const noexcept { return counters_; }

    [[nodiscard]] GpuCompletionTracker& gpu_completion() noexcept { return *gpu_completion_; }
    [[nodiscard]] FrameSlotPool& slots() noexcept { return *slots_; }
    [[nodiscard]] FrameQueue& evaluated_queue() noexcept { return *evaluated_queue_; }
    [[nodiscard]] FrameQueue& rendered_queue() noexcept { return *rendered_queue_; }

    void reset_temporal_history() noexcept {
        frame_history = FrameHistorySlot{};
        dirty_telemetry.previous_layers.clear();
    }

    [[nodiscard]] DirtyHistory& dirty_history() noexcept { return dirty_telemetry; }
    [[nodiscard]] const DirtyHistory& dirty_history() const noexcept {
        return dirty_telemetry;
    }

    void reset_frame_counters() noexcept {
        dirty_telemetry.reset_telemetry_counters();
    }

    void clear_last_frame_error() { frame_error_slot->clear(); }
    void publish_last_frame_error(const graph::NodeExecutionError& error) {
        frame_error_slot->publish_first(error);
    }
    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError>
    last_frame_error() const { return frame_error_slot->load(); }

    void reset_frame_temporaries() {
        dirty_telemetry.reset_telemetry_counters();
        clear_last_frame_error();
    }
    void reset_frame_values() {
        reset_frame_temporaries();
        program_store_state->clear();
        layout_cache.clear();
        memory_tracker->reset();
    }
    void reset_scene_state() noexcept { scene_hasher_state = graph::SceneHasher{}; }
    [[nodiscard]] graph::SceneHasher& scene_hasher() noexcept { return scene_hasher_state; }
    [[nodiscard]] const graph::SceneHasher& scene_hasher() const noexcept { return scene_hasher_state; }
    [[nodiscard]] graph::SceneProgramStore& program_store() noexcept { return *program_store_state; }
    [[nodiscard]] const graph::SceneProgramStore& program_store() const noexcept { return *program_store_state; }
    [[nodiscard]] FrameArena& arena() noexcept { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const noexcept { return *arena_ptr; }
    [[nodiscard]] FrameHistoryState history_state() noexcept {
        return FrameHistoryState{frame_history, dirty_telemetry};
    }

    // Canonical job-owned state. `FrameExecution` borrows these fields and
    // promotes only aggregate counters when its lease ends.
    FrameHistorySlot frame_history{};
    DirtyHistorySlot dirty_telemetry{};
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};
    std::unique_ptr<graph::ExecutionWorkspaceRing> execution_workspaces{
        std::make_unique<graph::ExecutionWorkspaceRing>()};
    graph::SceneHasher scene_hasher_state{};
    std::unique_ptr<graph::SceneProgramStore> program_store_state{
        std::make_unique<graph::SceneProgramStore>()};
    TextLayoutCache layout_cache{};
    std::unique_ptr<graph::NodeMemoryTracker> memory_tracker{
        std::make_unique<graph::NodeMemoryTracker>()};
    std::unique_ptr<RenderErrorSlot> frame_error_slot{
        std::make_unique<RenderErrorSlot>()};
    const CompiledComposition* prepared_composition{nullptr};
    std::string authoring_composition_name{};
    std::uint64_t authoring_composition_identity{0};
    std::shared_ptr<const CompiledComposition> authoring_compiled_composition{nullptr};

    void reset_job();

private:
    RuntimeContext* runtime_{nullptr};
    graph::GraphExecutor* executor_{nullptr};
    std::shared_ptr<const compiler::CompiledProgram> program_;
    Frame first_frame_{0};
    Frame end_frame_{0};
    std::uint64_t job_id_{0};
    CancellationToken cancellation_;
    JobCounters counters_{};
    std::unique_ptr<GpuCompletionTracker> gpu_completion_{
        std::make_unique<GpuCompletionTracker>(kPipelineDepth)};
    std::unique_ptr<FrameSlotPool> slots_{
        std::make_unique<FrameSlotPool>(kPipelineDepth, *gpu_completion_)};
    std::unique_ptr<FrameQueue> evaluated_queue_{
        std::make_unique<FrameQueue>(kPipelineDepth)};
    std::unique_ptr<FrameQueue> rendered_queue_{
        std::make_unique<FrameQueue>(kPipelineDepth)};
};

}  // namespace chronon3d::runtime
