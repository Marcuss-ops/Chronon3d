#include "layer_item.hpp"

namespace chronon3d::graph::detail {

LayerGraphItem make_layer_graph_item_for_refresh(
    const ResolvedLayer& resolved_layer,
    const RenderGraphContext& ctx)
{
    const Layer& layer = *resolved_layer.layer;

    if (ctx.frame_input.camera_2_5d.enabled && layer.uses_2_5d_projection) {
        Transform effective_transform = resolved_layer.world_transform;
        const Mat4 projection_world_matrix = effective_transform.to_mat4();
        auto proj = project_layer_2_5d(
            effective_transform,
            projection_world_matrix,
            ctx.frame_input.camera_2_5d,
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height),
            ctx.policy.diagnostics_enabled
        );
        if (proj.visible) {
            const Mat4 eff_proj = is_native_3d_layer(layer)
                ? Mat4(1.0f)
                : proj.transform.to_mat4();
            return LayerGraphItem{
                .layer             = resolved_layer.layer,
                .transform         = proj.transform,
                .world_matrix      = resolved_layer.world_matrix,
                .projection_matrix = eff_proj,
                .depth             = proj.depth,
                .world_z           = resolved_layer.world_transform.position.z,
                .projected         = true,
                .native_3d         = is_native_3d_layer(layer),
                .insertion_index   = resolved_layer.insertion_index,
                .matte_node        = k_invalid_node,
            };
        }
    }

    return LayerGraphItem{
        .layer           = resolved_layer.layer,
        .transform       = resolved_layer.world_transform,
        .world_matrix    = resolved_layer.world_matrix,
        .depth           = 0.0f,
        .world_z         = resolved_layer.world_transform.position.z,
        .projected       = false,
        .native_3d       = is_native_3d_layer(layer),
        .insertion_index = resolved_layer.insertion_index,
        .matte_node      = k_invalid_node,
    };
}

} // namespace chronon3d::graph::detail
