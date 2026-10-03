#include "graph_builder_pipeline.hpp"

#include "passes/graph_builder_layer_passes.hpp"
#include "passes/graph_builder_source_pass.hpp"
#include "evaluated_layer_placement.hpp"
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

namespace chronon3d::graph::detail {

GraphNodeId build_matte_sub_pipeline(
    RenderGraph& graph, const LayerGraphItem& item, const RenderGraphContext& ctx,
    const Camera2_5DRuntime& cam25d, bool include_layer_mask,
    bool include_layer_effects, bool include_layer_track_matte)
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
    const Layer& layer = *item.layer;
    const bool mask_before_transform =
        layer.kind == LayerKind::Normal || layer.kind == LayerKind::Shape ||
        layer.kind == LayerKind::Text || layer.kind == LayerKind::Precomp ||
        layer.kind == LayerKind::Video;
    if (include_layer_mask && mask_before_transform)
        append_mask_pass_if_needed(graph, out, item, ctx, node_ctx);
    append_transform_pass_if_needed(graph, out, item, ctx, node_ctx);
    if (include_layer_mask && !mask_before_transform)
        append_mask_pass_if_needed(graph, out, item, ctx, node_ctx);
    if (include_layer_effects)
        append_effect_pass_if_needed(graph, out, layer, item, cam25d, ctx, node_ctx);
    if (include_layer_track_matte && layer.track_matte.active()) {
        if (item.matte_node == k_invalid_node)
            throw std::runtime_error("nested track-matte source was not materialized: " +
                                     std::string(layer.name));
        cache::NodeCacheKey matte_key = cache::NodeCacheIdentityBuilder{
            "matte:" + std::string(layer.name)
        }
            .frame((layer.cache_static || item.is_static) ? Frame{0}
                                                          : ctx.frame_input.frame)
            .output(ctx.frame_input.width, ctx.frame_input.height)
            .params(hash_combine(
                hash_bytes(layer.track_matte.source_layer.data(),
                           layer.track_matte.source_layer.size()),
                static_cast<u64>(layer.track_matte.type)))
            .camera_if(ctx.frame_input.has_camera_2_5d,
                       ctx.frame_input.camera_2_5d)
            .build();
        GraphNodeId matte_node = graph.add_node(std::make_unique<TrackMatteNode>(
            layer.track_matte.type, std::string(layer.name), matte_key), node_ctx);
        graph.connect(out, matte_node);
        graph.connect(item.matte_node, matte_node);
        out = matte_node;
    }
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
