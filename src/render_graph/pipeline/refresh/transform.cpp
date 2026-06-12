#include "transform.hpp"
#include "layer_item.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/graph_builder_internal.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

void refresh_transform_node(
    TransformNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx)
{
    const std::string layer_id = node.layer_id();
    if (layer_id.empty()) return;

    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const ResolvedLayer& rl = *layer_it->second;
    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);

    if (item.projected) {
        // ── Projected path (2.5D camera) ───────────────────────────────
        node.set_matrix(item.projection_matrix);
        node.set_opacity(item.transform.opacity);
    } else {
        // ── Non-projected path ──────────────────────────────────────────
        Mat4 effective_matrix = item.world_matrix;

        if (should_use_centered_rendering(item, ctx)) {
            if (ctx.options.ssaa_factor > 1.0f) {
                Mat4 ssaa_world = item.world_matrix;
                ssaa_world[3][0] *= ctx.options.ssaa_factor;
                ssaa_world[3][1] *= ctx.options.ssaa_factor;
                ssaa_world[3][2] *= ctx.options.ssaa_factor;
                effective_matrix = ssaa_world;
            }
            effective_matrix =
                glm::translate(Mat4(1.0f), Vec3(-ctx.frame.width * 0.5f, -ctx.frame.height * 0.5f, 0.0f)) *
                effective_matrix;
        } else {
            // Delegate to the shared helper so the refresh-path stays in
            // sync with the build-path (graph_builder_layer_passes.cpp).
            const Mat4 stripped = strip_implicit_canvas_centering(
                effective_matrix, item, ctx);
            if (ctx.options.diagnostics_enabled &&
                (stripped[3][0] != effective_matrix[3][0] ||
                 stripped[3][1] != effective_matrix[3][1])) {
                spdlog::info("[refresh-transform] stripped centering translation for layer='{}' frame={}",
                    layer_id, static_cast<int>(ctx.frame.frame));
            }
            effective_matrix = stripped;
        }

        node.set_matrix(effective_matrix);
        node.set_opacity(item.transform.opacity);
    }
}

} // namespace chronon3d::graph::detail
