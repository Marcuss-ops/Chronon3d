#include "transform.hpp"
#include "layer_item.hpp"

#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/evaluated_layer_placement.hpp"
#include "../../builder/graph_builder_internal.hpp"

namespace chronon3d::graph::detail {

void refresh_transform_node(
    TransformNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx)
{
    const std::string layer_id{node.layer_id()};
    if (layer_id.empty()) return;

    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const ResolvedLayer& rl = *layer_it->second;
    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
    const auto placement = evaluate_layer_placement(item, ctx);

    node.set_matrix(placement.render_matrix);
    node.set_opacity(placement.opacity);
}

} // namespace chronon3d::graph::detail
