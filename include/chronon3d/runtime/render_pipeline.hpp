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

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <string>
#include <unordered_map>

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
    [[nodiscard]] std::shared_ptr<Framebuffer> render(
        JobSession& session, Frame frame);

    /// Compatibility-only lifecycle for an arbitrary Composition callback.
    /// It is kept outside JobSession until that authoring API has a typed
    /// lowering into CompiledProgram.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_compatibility(
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
    SoftwareRenderer* m_renderer;
    ::chronon3d::SoftwareExecutionContext& m_context;
    // Bounded topology cache: Scene is a legacy graph input adapter, never a
    // frame-varying value. Its lifetime must not depend on FrameContext's arena.
    std::shared_mutex m_topology_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<const Scene>> m_topologies;
};

} // namespace chronon3d::runtime
