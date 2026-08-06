#include "graph_builder_pipeline.hpp"

#include "passes/graph_builder_layer_passes.hpp"
#include "passes/graph_builder_source_pass.hpp"
#include "evaluated_layer_placement.hpp"
#include <chronon3d/scene/model/layer/layer.hpp>

namespace chronon3d::graph::detail {

GraphNodeId build_matte_sub_pipeline(
    RenderGraph& graph, const LayerGraphItem& item, const RenderGraphContext& ctx)
{
    BuilderContext node_ctx{
        .layer_id = std::string(item.layer->name),
        .layer_index = static_cast<std::uint32_t>(item.insertion_index),
        .item_index = 0,
    };
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
    const Camera2_5DRuntime&,
    const std::unordered_map<std::string, bool>& is_static_cache)
{
    const bool is_static_val = is_static_cache.at(std::string(rl.layer->name));
    return resolve_layer_graph_item(rl, ctx, is_static_val);
}

} // namespace chronon3d::graph::detail
