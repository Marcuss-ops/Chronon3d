#include <chronon3d/runtime/scene_to_render_graph.hpp>
#include <chronon3d/math/projector_2_5d.hpp>

namespace chronon3d::runtime {

graph::RenderGraph build_render_graph_from_scene(
    const Scene& scene,
    const graph::RenderGraphContext& ctx
) {
    graph::RenderGraphContext bridge_ctx = ctx;
    bridge_ctx.light_context = scene.light_context();
    
    if (scene.camera_2_5d().enabled) {
        bridge_ctx.camera_2_5d = scene.camera_2_5d();
        bridge_ctx.has_camera_2_5d = true;
        bridge_ctx.projection_ctx = renderer::make_projection_context(
            bridge_ctx.camera_2_5d,
            bridge_ctx.width,
            bridge_ctx.height
        );
        bridge_ctx.projection_ctx.ready = true;
    }
    
    return graph::GraphBuilder::build(scene, bridge_ctx);
}

} // namespace chronon3d::runtime
