#include "multi_source.hpp"
#include "layer_item.hpp"

#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/evaluated_layer_placement.hpp"
#include "../../builder/graph_builder_internal.hpp"
#include "../../builder/passes/graph_builder_source_pass.hpp"

namespace chronon3d::graph::detail {

void refresh_multi_source_node(
    MultiSourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx)
{
    const std::string layer_id{node.layer_id()};
    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const ResolvedLayer& rl = *layer_it->second;
    const Layer& layer = *rl.layer;
    const bool source_layer_kind =
        layer.kind == LayerKind::Normal ||
        layer.kind == LayerKind::Shape ||
        layer.kind == LayerKind::Text;
    if (!source_layer_kind || layer.nodes.size() <= 1) {
        return;
    }

    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
    const std::string layer_name_str(layer.name);
    const bool item_static = is_static_cache.count(layer_name_str)
        ? is_static_cache.at(layer_name_str) : layer.cache_static;
    const bool source_is_static = item_static;
    std::vector<MultiSourceItem> items;
    items.reserve(layer.nodes.size());
    u64 aggregated_params_hash = 0;
    std::vector<RenderNode> materialized_nodes;
    materialized_nodes.reserve(layer.nodes.size());
    for (const auto& authored_node : layer.nodes) {
        materialized_nodes.push_back(materialize_mesh_node(authored_node, ctx));
    }
    for (const auto& src_node : materialized_nodes) {
        const auto source_placement = evaluate_source_placement(item, src_node, ctx);
        const Mat4 render_matrix = finalize_source_placement_matrix(
            source_placement, item, src_node, ctx);
        const f32 render_opacity = source_placement.opacity;

        items.push_back(MultiSourceItem{
            .node = &src_node,
            .matrix = render_matrix,
            .opacity = render_opacity,
            .defer_camera_projection = item.projected && !item.native_3d &&
                !item.layer->screen_space,
            .apply_camera_projection = !item.layer->screen_space,
            .native_3d = item.native_3d,
        });
        aggregated_params_hash = hash_combine(aggregated_params_hash, hash_render_node(src_node));
        // Deliberately does NOT fold `hash_text_run_shape`
        // here.  `MultiSourceNode::cache_key()` re-folds it per item at
        // evaluation time so per-frame animator mutations invalidate the
        // entry correctly.  Folding it here would DUPLICATE the bytes
        // (source-pass already avoided the fold for the same reason)
        // and stale out as soon as the shape mutates between refreshes.
    }

    // Canonical cache identity (TICKET-ae-cam-hash-collision Soluzione B):
    // the builder folds the evaluated camera by construction so this
    // MultiSourceNode's framebuffer cache distinguishes zoom-animated frames
    // (AE_CAM_02) and parent-relative Z-dolly frames (AE_CAM_04).
    cache::NodeCacheKey key = cache::NodeCacheIdentityBuilder{
        "layer.multisource:" + layer_name_str
    }
        .frame(source_is_static ? Frame{0} : ctx.frame_input.frame)
        .output(ctx.frame_input.width, ctx.frame_input.height)
        .params(aggregated_params_hash)
        .source(hash_string(layer_name_str + "_multisource"))
        .camera_if(ctx.frame_input.has_camera_2_5d,
                   ctx.frame_input.camera_2_5d)
        .build();

    node.refresh(
        layer_name_str + "_multi",
        std::move(items),
        key,
        node.cache_policy()
    );
}

} // namespace chronon3d::graph::detail
