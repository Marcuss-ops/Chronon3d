#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/error.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/effects/effect_type.hpp>
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
#include <chronon3d/runtime/render_surface.hpp>

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <mutex>
#include <string_view>
#include <vector>

namespace chronon3d::graph {
struct NodeExecutionError;
class GraphExecutor;
class RenderBackend;
}

namespace chronon3d::runtime {

class TemporalSampleWorkspace;

class RuntimeContext;
struct NativeEchoHistoryState;

/// Job-owned device-local copy of one Echo input frame. Shared references keep
/// a surface alive while another frame is consuming it from the GPU queue.
struct NativeEchoSurface {
    RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
    SurfaceDesc desc{};
    ~NativeEchoSurface();

private:
    graph::RenderBackend* backend{nullptr};
    RenderSurfaceRegistry* registry{nullptr};
    friend class JobSession;
};
using NativeEchoSurfaceRef = std::shared_ptr<const NativeEchoSurface>;

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

/// Per-job route telemetry. EffectType values are explicitly assigned and
/// sparse; the fixed array is indexed by the stable numeric value, with slot 0
/// reserved for Unknown. Counters observe route decisions only and never
/// participate in dispatch/admission. kEffectTypeCount must cover the catalog's
/// highest explicitly assigned enum value (currently 47).
inline constexpr std::size_t kEffectTypeCount = 48;
struct AtomicEffectUsagePair {
    std::atomic<std::uint64_t> native_frames{0};
    std::atomic<std::uint64_t> admission_rejected_frames{0};
};
struct EffectUsageCounters {
    std::array<AtomicEffectUsagePair, kEffectTypeCount> by_type{};

    EffectUsageCounters() = default;
    EffectUsageCounters(const EffectUsageCounters&) = delete;
    EffectUsageCounters& operator=(const EffectUsageCounters&) = delete;
    EffectUsageCounters(EffectUsageCounters&& other) noexcept {
        for (std::size_t index = 0; index < by_type.size(); ++index) {
            by_type[index].native_frames.store(
                other.by_type[index].native_frames.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            by_type[index].admission_rejected_frames.store(
                other.by_type[index].admission_rejected_frames.load(
                    std::memory_order_relaxed), std::memory_order_relaxed);
        }
    }
    EffectUsageCounters& operator=(EffectUsageCounters&& other) noexcept {
        if (this == &other) return *this;
        for (std::size_t index = 0; index < by_type.size(); ++index) {
            by_type[index].native_frames.store(
                other.by_type[index].native_frames.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            by_type[index].admission_rejected_frames.store(
                other.by_type[index].admission_rejected_frames.load(
                    std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    void record_native(effects::EffectType type) noexcept {
        const auto index = static_cast<std::size_t>(type);
        if (index < by_type.size())
            by_type[index].native_frames.fetch_add(1, std::memory_order_relaxed);
    }
    void record_admission_rejected(effects::EffectType type) noexcept {
        const auto index = static_cast<std::size_t>(type);
        if (index < by_type.size())
            by_type[index].admission_rejected_frames.fetch_add(
                1, std::memory_order_relaxed);
    }
    void reset() noexcept {
        for (auto& pair : by_type) {
            pair.native_frames.store(0, std::memory_order_relaxed);
            pair.admission_rejected_frames.store(0, std::memory_order_relaxed);
        }
    }
};

enum class JobState : std::uint8_t {
    Ready,
    Running,
    Completed,
    Failed,
};

/// Job-lifetime state. The runtime context is borrowed.  History and pipeline
/// authorities belong to this job; frame-local state is leased separately by
/// FrameExecution. Runtime/backend/surface-registry objects used by retained
/// native echo surfaces must outlive this session; destruction releases those
/// surfaces while their owners are still available.
class JobSession final {
public:
    static constexpr std::size_t kPipelineDepth = 3;

    /// Empty compatibility session for standalone graph diagnostics. It is
    /// intentionally not a render job until a RuntimeContext is bound.
    JobSession();
    explicit JobSession(RuntimeContext& runtime);
    JobSession(RuntimeContext& runtime, JobSessionConfig config);
    /// Compatibility-only session for arbitrary Composition callbacks. It
    /// deliberately has no compiler program; typed production execution must
    /// use the JobSessionConfig constructor above.
    JobSession(RuntimeContext& runtime, Frame first_frame, Frame end_frame,
               std::uint64_t job_id);
    /// Create a lightweight isolated session for one temporal sample. It owns
    /// one arena slot because it never enters the job's frame scheduler.
    [[nodiscard]] static std::unique_ptr<JobSession>
    make_temporal_sample_session();
    ~JobSession();

    JobSession(const JobSession&) = delete;
    JobSession& operator=(const JobSession&) = delete;
    JobSession(JobSession&&) noexcept;
    JobSession& operator=(JobSession&&) noexcept;

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
    [[nodiscard]] EffectUsageCounters& effect_usage() noexcept {
        return effect_usage_;
    }
    [[nodiscard]] const EffectUsageCounters& effect_usage() const noexcept {
        return effect_usage_;
    }

    /// Start a new explicit job boundary. A session can be reused for a
    /// sequence of independent renders, so this clears only the prior job
    /// outcome; frame-local diagnostics remain separately owned.
    void begin_job() {
        std::lock_guard lock(status_->mutex);
        status_->state = JobState::Running;
        status_->error.reset();
    }

    /// Canonical terminal transition. The first error is retained so later
    /// cleanup failures cannot obscure the root cause.
    void fail_job(chronon::Error error) {
        std::lock_guard lock(status_->mutex);
        if (!status_->error) status_->error = std::move(error);
        status_->state = JobState::Failed;
    }

    void complete_job() {
        std::lock_guard lock(status_->mutex);
        if (status_->state != JobState::Failed) status_->state = JobState::Completed;
    }

    [[nodiscard]] JobState state() const {
        std::lock_guard lock(status_->mutex);
        return status_->state;
    }

    [[nodiscard]] std::optional<chronon::Error> job_error() const {
        std::lock_guard lock(status_->mutex);
        return status_->error;
    }

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
            // Queue publication failed after the forward transition. The
            // FrameSlotPool remains the sole state writer, including rollback.
            (void)slots_->transition(slot, FrameSlotState::Evaluated,
                                     FrameSlotState::Evaluating);
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
            // Roll back through the same CAS transition authority; no direct
            // state store is permitted outside FrameSlotPool.
            (void)slots_->transition(slot, FrameSlotState::Rendered,
                                     FrameSlotState::Evaluated);
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
        clear_native_echo_history();
    }

    [[nodiscard]] std::vector<NativeEchoSurfaceRef> native_echo_frames(
        std::string_view key, std::int64_t current_frame,
        std::uint32_t echoes, std::uint32_t frame_offset) const;
    [[nodiscard]] bool reserve_native_echo_history(
        std::string_view key, std::size_t bytes_per_frame,
        std::size_t keep_frames);
    [[nodiscard]] std::optional<std::array<RenderSurfaceHandle, 3>>
    native_echo_scratch(std::string_view key, FrameSlotId slot_id,
                        const SurfaceDesc& desc, graph::RenderBackend& backend,
                        RenderSurfaceRegistry& registry);
    [[nodiscard]] TemporalSampleWorkspace& temporal_sample_workspace(
        FrameSlotId slot_id);
    [[nodiscard]] bool record_native_echo_frame(
        std::string_view key, std::int64_t frame,
        RenderSurfaceHandle source, std::size_t keep_frames,
        graph::RenderBackend& backend, RenderSurfaceRegistry& registry);
    void clear_native_echo_history() noexcept;

    [[nodiscard]] DirtyHistory& dirty_history() noexcept { return dirty_telemetry; }
    [[nodiscard]] const DirtyHistory& dirty_history() const noexcept {
        return dirty_telemetry;
    }
    [[nodiscard]] ResourcePlan& resource_plan() noexcept { return resource_plan_state; }
    [[nodiscard]] const ResourcePlan& resource_plan() const noexcept { return resource_plan_state; }
    [[nodiscard]] graph::NodeMemoryTracker& memory_aggregate() noexcept {
        return *memory_aggregate_state;
    }
    [[nodiscard]] const graph::NodeMemoryTracker& memory_aggregate() const noexcept {
        return *memory_aggregate_state;
    }

    void reset_frame_counters() noexcept {
        dirty_telemetry.reset_telemetry_counters();
    }

    void clear_last_frame_error() { frame_error_aggregate->clear(); }
    void publish_last_frame_error(const graph::NodeExecutionError& error) {
        frame_error_aggregate->publish_first(error);
    }
    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError>
    last_frame_error() const { return frame_error_aggregate->load(); }

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
    // Standalone graph diagnostics have no leased FrameExecution. Keep their
    // slot-zero ownership explicit; production frame work must use
    // FrameExecution::arena() so it follows the active lease.
    [[nodiscard]] FrameArena& root_arena() noexcept { return slots_->payload(0).arena; }
    [[nodiscard]] const FrameArena& root_arena() const noexcept {
        return slots_->payload(0).arena;
    }
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
    std::unique_ptr<graph::NodeMemoryTracker> memory_aggregate_state{
        std::make_unique<graph::NodeMemoryTracker>()};
    // Job-level diagnostic aggregate; frame-local writes use the slot's
    // RenderErrorSlot and are promoted at execution completion.
    std::unique_ptr<RenderErrorSlot> frame_error_aggregate{
        std::make_unique<RenderErrorSlot>()};

    /// Reusable job-owned download scratch buffer for GPU→CPU readback.
    /// Lifetime: job-scoped, high-water-mark retained. The buffer grows to
    /// the peak `required_floats` seen during the job and is never shrunk
    /// mid-job to avoid per-frame realloc (FIXING.md H-07). It is released
    /// only by `reset_job()` (job teardown) which may `shrink_to_fit()` if
    /// the peak exceeded a threshold (e.g. 16 MiB) to bound idle memory.
    [[nodiscard]] std::span<float> download_scratch(std::size_t required_floats) {
        if (download_scratch_.size() < required_floats) {
            download_scratch_.resize(required_floats);
        }
        return std::span<float>(download_scratch_.data(), required_floats);
    }

    void reset_job();

private:
    struct TemporalSampleTag {};
    explicit JobSession(TemporalSampleTag);
    void initialize_slot_authorities(
        std::size_t capacity,
        std::size_t arena_capacity_bytes = 4u * 1024u * 1024u);

    struct JobStatus {
        mutable std::mutex mutex;
        JobState state{JobState::Ready};
        std::optional<chronon::Error> error;
    };

    // FIXING.md H-07 — job-lifetime high-water-mark buffer. Grows on demand,
    // never shrinks until reset_job(). See download_scratch() doc above.
    std::vector<float> download_scratch_{};
    RuntimeContext* runtime_{nullptr};
    graph::GraphExecutor* executor_{nullptr};
    std::shared_ptr<const compiler::CompiledProgram> program_;
    Frame first_frame_{0};
    Frame end_frame_{0};
    std::uint64_t job_id_{0};
    CancellationToken cancellation_;
    JobCounters counters_{};
    EffectUsageCounters effect_usage_{};
    std::shared_ptr<JobStatus> status_{std::make_shared<JobStatus>()};
    std::shared_ptr<NativeEchoHistoryState> native_echo_history_;
    std::array<std::unique_ptr<TemporalSampleWorkspace>, kPipelineDepth>
        temporal_sample_workspaces_{};
    std::unique_ptr<GpuCompletionTracker> gpu_completion_{};
    std::unique_ptr<FrameSlotPool> slots_{};
    std::unique_ptr<FrameQueue> evaluated_queue_{};
    std::unique_ptr<FrameQueue> rendered_queue_{};
};

}  // namespace chronon3d::runtime
