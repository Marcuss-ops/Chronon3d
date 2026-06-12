#include "multi_source.hpp"
#include "layer_item.hpp"

#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/graph_builder_internal.hpp"

namespace chronon3d::graph::detail {

void refresh_multi_source_node(
    MultiSourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx)
{
    const std::string layer_id = node.layer_id();
    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const ResolvedLayer& rl = *layer_it->second;
    const Layer& layer = *rl.layer;
    if (layer.kind != LayerKind::Normal || layer.nodes.size() <= 1) {
        return;
    }

    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
    const bool use_local = ctx.options.modular_coordinates &&
        layer_needs_render_transform(item, ctx) &&
        !item.native_3d;
    const std::string layer_name_str(layer.name);
    const bool item_static = is_static_cache.count(layer_name_str)
        ? is_static_cache.at(layer_name_str) : layer.cache_static;
    const bool source_is_static = item_static || use_local;
    const Mat4 item_source_world = use_local
        ? item.world_matrix
        : source_space_world_matrix(item, ctx);

    std::vector<MultiSourceItem> items;
    items.reserve(layer.nodes.size());
    u64 aggregated_params_hash = 0;
    for (const auto& src_node : layer.nodes) {
        const Mat4 node_matrix = src_node.world_transform.to_mat4();
        const Mat4 render_matrix = use_local
            ? node_matrix
            : (item_source_world * node_matrix);
        const f32 render_opacity = use_local
            ? src_node.world_transform.opacity
            : (item.transform.opacity * src_node.world_transform.opacity);

        items.push_back(MultiSourceItem{
            .node = &src_node,
            .matrix = render_matrix,
            .opacity = render_opacity
        });
        aggregated_params_hash = hash_combine(aggregated_params_hash, hash_render_node(src_node));
    }

    cache::NodeCacheKey key{
        .scope = "layer.multisource:" + layer_name_str,
        .frame = source_is_static ? Frame{0} : ctx.frame.frame,
        .width = ctx.frame.width,
        .height = ctx.frame.height,
        .params_hash = aggregated_params_hash,
        .source_hash = hash_string(layer_name_str + "_multisource")
    };

    node.refresh(
        layer_name_str + "_multi",
        std::move(items),
        key,
        should_use_centered_rendering(item, ctx),
        item.projected,
        source_is_static
    );
}

} // namespace chronon3d::graph::detail
