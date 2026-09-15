#pragma once

// ===========================================================================
// runtime/render_pipeline.hpp
//
// TICKET-011a follow-up #1 — typed facade over the underlying graph
// orchestration functions.
//
// RenderPipeline holds a `(SoftwareRenderer*, SoftwareExecutionContext&)` pair
// constructed after the SoftwareBackend is attached in
// `RenderEngine::Impl`.  Its member methods delegate 1:1 to the
// existing free functions in `<chronon3d/render_graph/pipeline/
// render_pipeline.hpp>` (`graph::render_scene_via_graph`,
// `graph::render_composition_frame`, `graph::debug_scene_graph`),
// but reads already-resolved dependency views from the execution context.
//
// Why a class and not a free function:
//   - Encapsulates the (renderer, execution-context) pair wiring so callers
//     cannot accidentally mismatch them.
//   - Provides a stable surface for SDK consumers once the
//     orchestrating free functions are demoted to internal-virtual.
//   - Aligns with the `RenderEngine::Impl` ownership tree:
//
//        chronon3d::RenderEngine
//          └── RenderEngine::Impl (PIMPL)
//              ├── runtime::RuntimeContext    (engine-lifetime owner)
//              ├── SoftwareRenderer          (per-instance orchestrator)
//              └── runtime::RenderPipeline   (this class)
//
//     `Impl` constructs the pipeline AFTER the renderer + backend
//     are wired.
// ===========================================================================

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace chronon3d {
class SoftwareRenderer;
struct SoftwareExecutionContext;
struct CompiledComposition;
namespace compiler { class CompiledProgram; }
namespace media { class MediaFrameProvider; }
}

namespace chronon3d::runtime {
class FrameExecution;
class JobSession;

using FrameEncoder = std::function<bool(Frame, const Framebuffer&)>;

struct RenderBatchResult {
    bool ok{true};
    std::size_t frames_rendered{0};
    std::size_t frames_encoded{0};
    std::size_t max_queue_depth{0};
    Frame failed_frame{-1};
    std::string error{};
};

/// Expected, non-exceptional outcomes of the single-frame scheduling
/// lifecycle.  These are backpressure/normal states, not job failures: the
/// JobSession already records the canonical `Error` for each.
///
/// Rationale (audit N5): the per-frame reach must not throw for states the
/// caller can be expected to retry or report, and a caller must be able to
/// distinguish "no free slot" from "publish failed" without matching on an
/// exception message.
enum class FrameSchedulingCode : std::uint8_t {
    NoFreeFrameSlot = 0,
    PublishEvaluatedFailed,
    AcquireRenderSlotFailed,
    PublishRenderedFailed,
    ReleaseEncodedSlotFailed,
    ExecutorFailed,
};

[[nodiscard]] const char* frame_scheduling_code_name(
    FrameSchedulingCode code) noexcept;

/// Classification of a scheduling outcome — the axis a caller must branch on
/// instead of matching an exception message.
///
/// `Backpressure` is the ONLY retryable class. Every other class means the
/// pipeline was not simply busy, so re-submitting the same frame unchanged is
/// not a legitimate response.
enum class FrameSchedulingClass : std::uint8_t {
    /// The pipeline is saturated: `acquire_for_evaluation()` found no free
    /// frame slot. The non-exceptional backpressure state — wait for the
    /// pipeline to drain and re-submit the same frame.
    Backpressure = 0,
    /// The slot authority refused a transition the pipeline had just made
    /// legal, or handed back an element the pipeline had just accepted.
    ///
    /// Both are impossible while the pipeline owns both sides of the hand-off:
    /// `publish_evaluated()`/`publish_rendered()` only fail after their CAS
    /// succeeded, and the queues and the slot pool share one capacity
    /// (`kPipelineDepth`), so a full push would require the CAS to have failed
    /// first. The session is left consistent (the operation is rolled back),
    /// which is exactly why this must be reported rather than retried.
    InvariantViolation,
    /// The frame's own execution failed. A canonical `Error` is already
    /// recorded on the JobSession; the pipeline is not at fault.
    JobFailure,
};

[[nodiscard]] constexpr FrameSchedulingClass frame_scheduling_class(
    FrameSchedulingCode code) noexcept {
    switch (code) {
        case FrameSchedulingCode::NoFreeFrameSlot:
            return FrameSchedulingClass::Backpressure;
        case FrameSchedulingCode::PublishEvaluatedFailed:
        case FrameSchedulingCode::AcquireRenderSlotFailed:
        case FrameSchedulingCode::PublishRenderedFailed:
        case FrameSchedulingCode::ReleaseEncodedSlotFailed:
            return FrameSchedulingClass::InvariantViolation;
        case FrameSchedulingCode::ExecutorFailed:
            return FrameSchedulingClass::JobFailure;
    }
    // A value outside the enum cannot be classified; treating it as a defect
    // is the only safe default (it must never be retried as backpressure).
    return FrameSchedulingClass::InvariantViolation;
}

/// The one question a scheduling caller actually asks.
[[nodiscard]] constexpr bool frame_scheduling_is_retryable(
    FrameSchedulingCode code) noexcept {
    return frame_scheduling_class(code) == FrameSchedulingClass::Backpressure;
}

[[nodiscard]] const char* frame_scheduling_class_name(
    FrameSchedulingClass scheduling_class) noexcept;

/// Canonical single-frame pipeline outcome.  `Result` is `[[nodiscard]]`, so a
/// call site cannot silently drop a scheduling failure.
using FrameRenderResult =
    ::chronon3d::Result<std::shared_ptr<Framebuffer>, FrameSchedulingCode>;
}

namespace chronon3d::runtime {

/// RenderPipeline — typed facade over the orchestrating graph paths.
///
/// Object lifetime is bound to a (renderer, runtime) pair created at
/// engine construction time.  Methods are thin pass-throughs to the
/// free-function implementations under
/// `<chronon3d/render_graph/pipeline/render_pipeline.hpp>`; the
/// facade exists for type safety, documentation, and forward-
/// compatibility with the TICKET-011 final-stage split where these
/// free functions become backend virtuals and the runtime holds the
/// only instance.
class RenderPipeline {
public:
    /// 06 R3b — pipeline accepts a SoftwareRenderer pointer rather than a
    /// `SoftwareRenderer &` to keep the public surface free of the
    /// boundary-gate I5 substring.  Pointer nullability is documented at
    /// the call site (the owning engine's renderer is always wired).
    RenderPipeline(SoftwareRenderer* renderer,
                   ::chronon3d::SoftwareExecutionContext& context) noexcept;

    /// Out-of-line so the PIMPL stage pool below stays an implementation detail
    /// (AUDIT F7).
    ~RenderPipeline();

    // ── Canonical prepared-plan execution entry ────────────────────
    /// Compatibility adapter for an immutable CompiledComposition. New typed
    /// production jobs use render(JobSession&, Frame) below.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_compiled_composition(
        const CompiledComposition& compiled, Frame frame,
        FrameExecution* execution = nullptr);

    [[nodiscard]] std::shared_ptr<Framebuffer> render_program(
        const compiler::CompiledProgram& program, Frame frame,
        FrameExecution* execution = nullptr);

    /// Owns the canonical single-frame lease lifecycle. PreparedRenderJob is
    /// only a handle; it must not duplicate slot, queue, or FrameExecution
    /// transitions here.
    ///
    /// Expected scheduling states are returned as `FrameSchedulingCode`, never
    /// thrown; only an unexpected backend exception escapes this call.
    [[nodiscard]] FrameRenderResult render(
        JobSession& session, Frame frame);

    /// Compatibility-only lifecycle for an arbitrary Composition callback.
    /// It is kept outside JobSession until that authoring API has a typed
    /// lowering into CompiledProgram.  Scheduling states follow `render()`:
    /// returned, not thrown.
    [[nodiscard]] FrameRenderResult render_compatibility(
        JobSession& session, const CompiledComposition& compiled, Frame frame);

    /// Canonical bounded batch lifecycle. PreparedRenderJob supplies only the
    /// immutable compatibility program and encoder callback; slot acquisition,
    /// stage queues, FrameExecution, and release remain runtime-owned.
    [[nodiscard]] RenderBatchResult render_compatibility_frames(
        JobSession& session, const CompiledComposition& compiled,
        Frame first, Frame count, const FrameEncoder& encoder);

    // ── Scene/debug adapters ───────────────────────────────────────
    /// @deprecated Fase C3 — scene authoring adapter.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const Camera& camera, i32 width, i32 height,
        float fps);

    /// @deprecated Fase C3 — scene authoring adapter.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const std::optional<Camera2_5D>& camera,
        i32 width, i32 height,
        float fps);

    // ── Diagnostics ───────────────────────────────────────────────
    /// @deprecated Fase C3 — diagnostics-only wrapper; use preflight tools instead.
    // TICKET-GATE-10-PHASE-4 fix-up: upstream `6df9b429` removed the `fps`
    // default but not from frames 5/6 (Frame, frame_time), violating the
    // C++ default-argument contiguity rule.  Sentinel `= 0.0f` restores
    // valid syntax with no new symbols (Cat-1 fix; preserves upstream
    // "no hardcoded fps literal" intent).
    [[nodiscard]] std::string debug_graph(
        const Scene& scene, const Camera& camera, i32 width, i32 height,
        float fps, Frame frame = 0, f32 frame_time = 0.0f);

private:
    /// AUDIT F7 — persistent executor for the batch pipeline's three stages.
    ///
    /// render_compatibility_frames() used to construct and join three
    /// std::thread objects per call, paying a spawn/join round trip (and losing
    /// worker affinity) for every batch. The three stage loops, their per-stage
    /// mutexes/CVs and the terminal join barrier are all unchanged; only the
    /// thread lifecycle is amortized: the workers are created once per pipeline,
    /// lazily on first use, and park on a condition variable between batches.
    /// The class is defined in the .cpp so no threading detail leaks into this
    /// header.
    class StagePool;
    std::unique_ptr<StagePool> m_stage_pool;

    SoftwareRenderer* m_renderer;
    ::chronon3d::SoftwareExecutionContext& m_context;
    // Bounded topology cache: Scene is a legacy graph input adapter, never a
    // frame-varying value. Its lifetime must not depend on FrameContext's arena.
    //
    // AUDIT F11 — this used to be a hand-rolled `unordered_map` with FIFO
    // eviction (`erase(begin())`). That violated two invariants at once: the
    // declared cache taxonomy ("Primitive canonica: LruCache — Nessuna seconda
    // cache engine") and the eviction semantics the call site needs — a FIFO
    // victim can be the most recently *used* topology whenever insert order
    // diverges from use order, and re-materializing one is a Scene::clone plus a
    // topology rebuild. It is now the canonical primitive in Count mode, with
    // the same 32-entry bound.
    static constexpr std::size_t kTopologyCacheCapacity = 32;

    cache::LruCache<std::uint64_t, std::shared_ptr<const Scene>> m_topologies{
        kTopologyCacheCapacity, /*num_shards=*/2, cache::CapacityMode::Count};
};

} // namespace chronon3d::runtime
