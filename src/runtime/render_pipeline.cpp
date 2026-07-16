// ===========================================================================
// runtime/render_pipeline.cpp
//
// RenderPipeline implementation — thin pass-through to the orchestrating
// graph free functions in `<chronon3d/render_graph/pipeline/render_pipeline.hpp>`.
// The facade exists for type safety + forward-compat with the TICKET-011
// final-stage split.
//
// All paths read the shared typed accessors on `m_runtime` (P1-15
// canonical surface: `runtime.node_cache()`, `runtime.backend()`, etc.)
// and per-instance state from `m_renderer` (SoftwareRenderer counts as
// the RenderBackend for graph dispatch).
// ===========================================================================

#include <chronon3d/runtime/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

namespace chronon3d::runtime {

// 06 R3b — SoftwareRenderer passed in by pointer (a single canonical type
// the RenderPipeline FACADE stores and forwards to graph free functions).
RenderPipeline::RenderPipeline(SoftwareRenderer* renderer, RenderRuntime& runtime) noexcept
    : m_renderer(renderer), m_runtime(runtime) {}

std::shared_ptr<Framebuffer> RenderPipeline::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height,
    float fps)
{
    // Pull shared caches/catalogs from the runtime; per-instance
    // state from the renderer (settings, registry, video_decoder).
    // The pipeline dispatches to the graph via the runtime's
    // attached backend (m_runtime.backend()), bypassing
    // SoftwareRenderer (06 R3b single-identity boundary).
    return chronon3d::graph::render_scene_via_graph(
        m_runtime.backend(),
        m_runtime.node_cache(),
        scene, camera, width, height,
        /*frame=*/0, /*frame_time=*/0.0f,
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
        fps, "scene", m_renderer
    );
}

std::shared_ptr<Framebuffer> RenderPipeline::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera,
    i32 width, i32 height,
    float fps)
{
    // 2.5D variant: clone the scene, stamp the optional camera
    // onto it, then route through the standard Scene path.  Mirrors
    // `SoftwareRenderer::render_scene(scene, optional<Camera2_5D>, …)`
    // semantics exactly.
    Scene effective = scene.clone();
    if (camera.has_value()) {
        effective.set_camera_2_5d(*camera);
    }
    Camera default_camera;
    return chronon3d::graph::render_scene_via_graph(
        m_runtime.backend(),
        m_runtime.node_cache(),
        effective, default_camera, width, height,
        /*frame=*/0, /*frame_time=*/0.0f,
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
        fps, "scene", m_renderer
    );
}

std::shared_ptr<Framebuffer> RenderPipeline::render_composition(
    const Composition& comp, Frame frame)
{
    return chronon3d::graph::render_composition_frame(
        m_runtime.backend(),
        m_runtime.node_cache(),
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
        comp, frame,
        m_renderer  /*R3 sidecar*/
    );
}

std::string RenderPipeline::debug_graph(
    const Scene& scene, const Camera& camera, i32 width, i32 height,
    float fps, Frame frame, f32 frame_time)
{
    return chronon3d::graph::debug_scene_graph(
        m_runtime.backend(),
        m_runtime.node_cache(),
        scene, camera, width, height,
        frame, frame_time,
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
        fps
    );
}

} // namespace chronon3d::runtime
