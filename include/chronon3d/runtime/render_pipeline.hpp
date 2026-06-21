#pragma once

// ===========================================================================
// runtime/render_pipeline.hpp
//
// TICKET-011a follow-up #1 — typed facade over the underlying graph
// orchestration functions.
//
// RenderPipeline holds a `(SoftwareRenderer &, RenderRuntime&)` pair
// constructed after the SoftwareBackend is attached in
// `RenderEngine::Impl`.  Its member methods delegate 1:1 to the
// existing free functions in `<chronon3d/render_graph/pipeline/
// render_pipeline.hpp>` (`graph::render_scene_via_graph`,
// `graph::render_composition_frame`, `graph::debug_scene_graph`),
// but read node cache / framebuffer pool / plan cache / scheduler
// from the runtime via `m_runtime->services()` so callers do not
// have to thread all four pointers on every call.
//
// Why a class and not a free function:
//   - Encapsulates the (renderer, runtime) pair wiring so callers
//     cannot accidentally mismatch them.
//   - Provides a stable surface for SDK consumers once the
//     orchestrating free functions are demoted to internal-virtual.
//   - Aligns with the `RenderEngine::Impl` ownership tree:
//
//        chronon3d::RenderEngine
//          └── RenderEngine::Impl (PIMPL)
//              ├── runtime::RenderRuntime    (engine-lifetime owner)
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

#include <memory>
#include <optional>
#include <string>

namespace chronon3d {
class SoftwareRenderer;
namespace media { class MediaFrameProvider; }
}

namespace chronon3d::runtime {
class RenderRuntime;
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
    /// Construct the pipeline with a (renderer, runtime) wired pair.
    /// Both references must outlive the pipeline instance.
    RenderPipeline(SoftwareRenderer & renderer, RenderRuntime& runtime) noexcept;

    // ── Primary rendering entry points ─────────────────────────────
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const Camera& camera, i32 width, i32 height);

    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const std::optional<Camera2_5D>& camera,
        i32 width, i32 height);

    [[nodiscard]] std::shared_ptr<Framebuffer> render_composition(
        const Composition& comp, Frame frame);

    // ── Diagnostics ───────────────────────────────────────────────
    [[nodiscard]] std::string debug_graph(
        const Scene& scene, const Camera& camera, i32 width, i32 height,
        Frame frame = 0, f32 frame_time = 0.0f);

private:
    SoftwareRenderer & m_renderer;
    RenderRuntime&    m_runtime;
};

} // namespace chronon3d::runtime
