#include <chronon3d/backends/software/software_registry.hpp>
// ---------------------------------------------------------------------------
// dirty/root_bbox_collector.cpp — Scene root node bbox computation
// ---------------------------------------------------------------------------

#include "root_bbox_collector.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::detail {

void compute_scene_root_bboxes(
    std::unordered_map<std::string, LayerBBoxState>& bboxes,
    const Scene& scene,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer
) {
    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
        const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
        Mat4 matrix;
        matrix = ssaa_scale * node.world_transform.to_mat4();
        if (ctx.policy.modular_coordinates && node.shape.type() == ShapeType::Line) {
            matrix = canvas_center * matrix;
        }
        auto* processor = sw_renderer->software_registry().get_shape(node.shape.type());
        if (!processor) continue;
        f32 spread = 0.0f;
        if (node.shadow.enabled) spread = std::max(spread, node.shadow.radius);
        if (node.glow.enabled)   spread = std::max(spread, node.glow.radius);
        raster::BBox bbox = processor->compute_world_bbox(node.shape, matrix, spread);

        LayerBBoxState state;
        state.bbox = bbox;
        state.world_matrix = node.world_transform.to_mat4();
        state.opacity = node.world_transform.opacity;
        state.visible = node.visible;
        state.cache_static = true;
        state.uses_2_5d_projection = false;
        state.content_hash = hash_render_node(node);
        bboxes["root.node:" + std::string(node.name)] = state;
    }
}

} // namespace chronon3d::graph::detail
