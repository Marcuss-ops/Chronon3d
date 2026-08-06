#include "layer_item.hpp"
#include "../../builder/evaluated_layer_placement.hpp"

namespace chronon3d::graph::detail {

LayerGraphItem make_layer_graph_item_for_refresh(
    const ResolvedLayer& resolved_layer,
    const RenderGraphContext& ctx)
{
    return resolve_layer_graph_item(resolved_layer, ctx);
}

} // namespace chronon3d::graph::detail
