#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/frame_history.hpp>
#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/frame_error_slot.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/internal/runtime/history_state.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/executor/execution_workspace.hpp>
#include <chronon3d/runtime/resource_plan.hpp>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <mutex>

namespace chronon3d::graph {
struct NodeExecutionError;
class GraphExecutor;
}

namespace chronon3d {
struct CompiledComposition;
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

struct JobSessionConfig {
    std::shared_ptr<const compiler::CompiledProgram> program;
    // Transitional execution view.  This belongs to the job lifetime rather
    // than the PreparedRenderJob handle and is removed with the legacy
    // composition executor once ProgramSampler is fully typed.
    std::shared_ptr<const ::chronon3d::CompiledComposition> legacy_composition;
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
    [[nodiscard]] const std::shared_ptr<const ::chronon3d::CompiledComposition>&
    legacy_composition() const noexcept { return legacy_composition_; }
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

    // Canonical frame-pipeline ownership. PreparedRenderJob coordinates these
    // transitions but does not own a second lease/queue implementation.
    [[nodiscard]] FrameSlotPayload& payload(const FrameExecutionSlot& slot) noexcept {
        return slots_->payload(slot.slot_id);
    }
    [[nodiscard]] FrameExecutionSlot* acquire_for_evaluation() noexcept {
        return slots_->try_acquire(FrameSlotState::Evaluating);
    }
    [[nodiscard]] bool publish_evaluated(FrameExecutionSlot& slot) noexcept {
        if (!slots_->transition(slot, FrameSlotState::Evaluating,
                                FrameSlotState::Evaluated)) {
            return false;
        }
        if (!evaluated_queue_->try_push(slot.slot_id)) {
            slots_->set_state(slot, FrameSlotState::Evaluating);
            return false;
        }
        return true;
    }
    [[nodiscard]] FrameExecutionSlot* acquire_for_render() noexcept {
        FrameSlotId slot_id = 0;
        return evaluated_queue_->try_pop(slot_id) ? &slots_->slot(slot_id) : nullptr;
    }
    [[nodiscard]] bool publish_rendered(FrameExecutionSlot& slot) noexcept {
        if (!slots_->transition(slot, FrameSlotState::Evaluated,
                                FrameSlotState::Rendered)) {
            return false;
        }
        if (!rendered_queue_->try_push(slot.slot_id)) {
            slots_->set_state(slot, FrameSlotState::Evaluated);
            return false;
        }
        return true;
    }
    [[nodiscard]] FrameExecutionSlot* acquire_for_encoding() noexcept {
        FrameSlotId slot_id = 0;
        return rendered_queue_->try_pop(slot_id) ? &slots_->slot(slot_id) : nullptr;
    }
    [[nodiscard]] bool begin_encoding(FrameExecutionSlot& slot) noexcept {
        return slots_->transition(slot, FrameSlotState::Rendered,
                                  FrameSlotState::Encoding);
    }
    void release_slot(FrameExecutionSlot& slot) noexcept {
        gpu_completion_->recycle(slot.slot_id);
        slot.native_surface_ptr = 0;
        slot.gpu_ready_sync = 0;
        slots_->release(slot);
    }
    [[nodiscard]] bool release_encoded(FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Encoding) {
            return false;
        }
        release_slot(slot);
        return true;
    }
    [[nodiscard]] bool abort(FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) == FrameSlotState::Free) {
            return false;
        }
        release_slot(slot);
        return true;
    }
    [[nodiscard]] std::size_t rendered_depth() const noexcept {
        return rendered_queue_->size();
    }
    void reset_slots() noexcept {
        evaluated_queue_->clear();
        rendered_queue_->clear();
        gpu_completion_->reset();
        slots_->reset();
    }

    void reset_temporal_history() noexcept {
        frame_history = FrameHistorySlot{};
        dirty_telemetry.previous_layers.clear();
    }

    [[nodiscard]] DirtyHistory& dirty_history() noexcept { return dirty_telemetry; }
    [[nodiscard]] const DirtyHistory& dirty_history() const noexcept {
        return dirty_telemetry;
    }
    [[nodiscard]] ResourcePlan& resource_plan() noexcept { return resource_plan_state; }
    [[nodiscard]] const ResourcePlan& resource_plan() const noexcept { return resource_plan_state; }
    [[nodiscard]] graph::NodeMemoryTracker& memory_aggregate() noexcept {
        return memory_aggregate_state;
    }
    [[nodiscard]] const graph::NodeMemoryTracker& memory_aggregate() const noexcept {
        return memory_aggregate_state;
    }

    void reset_frame_counters() noexcept {
        dirty_telemetry.reset_telemetry_counters();
    }

    void clear_last_frame_error() { frame_error_aggregate.clear(); }
    void publish_last_frame_error(const graph::NodeExecutionError& error) {
        frame_error_aggregate.publish_first(error);
    }
    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError>
    last_frame_error() const { return frame_error_aggregate.load(); }

    void reset_frame_temporaries() {
        dirty_telemetry.reset_telemetry_counters();
        clear_last_frame_error();
    }
    void reset_frame_values() {
        reset_frame_temporaries();
        // SceneProgramStore is a persistent program-cache authority. Frame
        // reset must not invalidate it; the active graph context owns the
        // explicit store seam used by precomp execution.
        // Per-frame memory telemetry is owned by the slot payload and is
        // reset when that lease is recycled; the job aggregate is reset only
        // at reset_job().
    }
    void reset_scene_state() noexcept { scene_hasher_state = graph::SceneHasher{}; }
    [[nodiscard]] graph::SceneHasher& scene_hasher() noexcept { return scene_hasher_state; }
    [[nodiscard]] const graph::SceneHasher& scene_hasher() const noexcept { return scene_hasher_state; }
    [[nodiscard]] graph::SceneProgramStore& program_store() noexcept { return *program_store_state; }
    [[nodiscard]] const graph::SceneProgramStore& program_store() const noexcept { return *program_store_state; }
    // Compatibility accessor for standalone graph diagnostics. Production
    // execution uses FrameExecution::arena(), which resolves to the leased
    // slot payload. Slot zero is the stable standalone payload for callers
    // that do not have a lease yet.
    [[nodiscard]] FrameArena& arena() noexcept { return slots_->payload(0).arena; }
    [[nodiscard]] const FrameArena& arena() const noexcept { return slots_->payload(0).arena; }
    [[nodiscard]] FrameHistoryState history_state() noexcept {
        return FrameHistoryState{frame_history, dirty_telemetry};
    }

    // Canonical job-owned state. `FrameExecution` borrows these fields and
    // promotes only aggregate counters when its lease ends.
    FrameHistorySlot frame_history{};
    DirtyHistorySlot dirty_telemetry{};
    ResourcePlan resource_plan_state{};
    graph::SceneHasher scene_hasher_state{};
    std::unique_ptr<graph::SceneProgramStore> program_store_state{
        std::make_unique<graph::SceneProgramStore>()};
    // Job-level aggregate. Individual node/frame records live in the leased
    // FrameSlotPayload and are promoted here when execution completes.
    graph::NodeMemoryTracker memory_aggregate_state{};
    // Job-level diagnostic aggregate; frame-local writes use the slot's
    // RenderErrorSlot and are promoted at execution completion.
    RenderErrorSlot frame_error_aggregate{};
    void reset_job();

private:
    RuntimeContext* runtime_{nullptr};
    graph::GraphExecutor* executor_{nullptr};
    std::shared_ptr<const compiler::CompiledProgram> program_;
    std::shared_ptr<const ::chronon3d::CompiledComposition> legacy_composition_;
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
