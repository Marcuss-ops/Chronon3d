#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/runtime/scene_to_render_graph.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "../render_graph/pipeline/helpers.hpp"

namespace chronon3d::runtime {

graph::RenderGraph build_render_graph_from_scene(
    const Scene& scene,
    const graph::RenderGraphContext& ctx
) {
    graph::RenderGraphContext bridge_ctx = ctx;
    bridge_ctx.camera.camera.light_context = scene.light_context();
    
    const auto resolved_camera = graph::resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        bridge_ctx.camera.camera.camera_2_5d = resolved_camera.camera;
        bridge_ctx.camera.camera.has_camera_2_5d = true;
        bridge_ctx.camera.camera.projection_ctx = renderer::make_projection_context(
            bridge_ctx.camera.camera.camera_2_5d,
            bridge_ctx.frame.frame.width,
            bridge_ctx.frame.frame.height
        );
        bridge_ctx.camera.camera.projection_ctx.ready = true;
    }
    
    return graph::GraphBuilder::build(scene, bridge_ctx);
}

} // namespace chronon3d::runtime
