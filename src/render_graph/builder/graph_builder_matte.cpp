#include "graph_builder_pipeline.hpp"

#include "passes/graph_builder_layer_passes.hpp"
#include "passes/graph_builder_source_pass.hpp"
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

namespace chronon3d::graph::detail {

GraphNodeId build_matte_sub_pipeline(
    RenderGraph& graph, const LayerGraphItem& item, const RenderGraphContext& ctx)
{
    BuilderContext node_ctx{.layer_id = std::string(item.layer->name)};
    GraphNodeId out = append_source_pass(graph, item, ctx, node_ctx);
    if (out == k_invalid_node) {
        return k_invalid_node;
    }
    append_transform_pass_if_needed(graph, out, item, ctx, node_ctx);
    return out;
}

LayerGraphItem make_item_for_matte_source(
    const ResolvedLayer& rl,
    const RenderGraphContext& ctx,
    const Camera2_5DRuntime& cam25d,
    const std::unordered_map<std::string, bool>& is_static_cache)
{
    const bool is_static_val = is_static_cache.at(std::string(rl.layer->name));
    if (cam25d.enabled && rl.layer->uses_2_5d_projection) {
        Transform effective_transform = rl.world_transform;
        const Mat4 projection_world_matrix = effective_transform.to_mat4();
        auto proj = project_layer_2_5d(
            effective_transform,
            projection_world_matrix,
            cam25d,
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height),
            ctx.policy.diagnostics_enabled
        );
        if (proj.visible) {
            const Mat4 eff_proj = is_native_3d_layer(*rl.layer)
                ? Mat4(1.0f)
                : glm::translate(Mat4(1.0f), Vec3(proj.transform.position.x, proj.transform.position.y, 0.0f)) * glm::scale(Mat4(1.0f), Vec3(proj.perspective_scale, proj.perspective_scale, 1.0f));
            return LayerGraphItem{
                .layer             = rl.layer,
                .transform         = proj.transform,
                .world_matrix      = rl.world_matrix,
                .projection_matrix = eff_proj,
                .depth             = proj.depth,
                .world_z           = rl.world_transform.position.z,
                .projected         = true,
                .native_3d         = is_native_3d_layer(*rl.layer),
                .insertion_index   = rl.insertion_index,
                .matte_node        = k_invalid_node,
                .is_static         = is_static_val,
            };
        }
    }
    return LayerGraphItem{
        .layer           = rl.layer,
        .transform       = rl.world_transform,
        .world_matrix    = rl.world_matrix,
        .depth           = 0.0f,
        .world_z         = rl.world_transform.position.z,
        .projected       = false,
        .native_3d       = is_native_3d_layer(*rl.layer),
        .insertion_index = rl.insertion_index,
        .matte_node      = k_invalid_node,
        .is_static       = is_static_val,
    };
}

} // namespace chronon3d::graph::detail
