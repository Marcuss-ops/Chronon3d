#include <chronon3d/backends/software/software_registry.hpp>
// ---------------------------------------------------------------------------
// dirty/root_bbox_collector.cpp — Scene root node bbox computation
// ---------------------------------------------------------------------------

#include "root_bbox_collector.hpp"
#include "layer_bbox_collector.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "../../builder/evaluated_layer_placement.hpp"

namespace chronon3d::graph::detail {

void compute_scene_root_bboxes(
    std::unordered_map<std::string, LayerBBoxState>& bboxes,
    const Scene& scene,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer
) {
    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        const auto placement = evaluate_root_source_placement(
            node, ctx, node.name, "root_bbox");
        if (!placement) continue;
        const Mat4 matrix = placement->render_matrix;
        const auto snapshot = sw_renderer->software_registry().snapshot();
        const auto processor = snapshot->shape_shared(
            snapshot->shape_handle(node.shape.type()));
        if (!processor) continue;
        f32 spread = 0.0f;
        raster::BBox bbox = processor->compute_world_bbox(node.shape, matrix, spread);

        LayerBBoxState state;
        state.bbox = bbox;
        state.world_matrix = node.world_transform.to_mat4();
        state.opacity = node.world_transform.opacity;
        state.visible = node.visible;
        state.cache_static = true;
        state.uses_2_5d_projection = ctx.frame_input.has_camera_2_5d;
        state.content_hash = hash_render_node(node);
        populate_node_semantic_fingerprints(node, state);
        state.structure_hash = hash_combine(
            state.structure_hash, hash_string("root.node"));
        state.semantic_fingerprints_valid = true;
        bboxes["root.node:" + std::string(node.name)] = state;
    }
}

} // namespace chronon3d::graph::detail
