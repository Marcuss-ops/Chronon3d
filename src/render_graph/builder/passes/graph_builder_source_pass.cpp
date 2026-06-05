#include "graph_builder_source_pass.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <memory>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;
    const bool is_static = layer.cache_static || item.is_static;

    if (layer.kind == LayerKind::Adjustment) {
        return k_invalid_node;
    }

    if (layer.kind == LayerKind::Normal) {
        if (layer.nodes.empty()) {
            return graph.add_node(std::make_unique<ClearNode>());
        }

        const bool layer_needs_transform = layer_needs_render_transform(item, ctx);
        const bool use_local = ctx.modular_coordinates && layer_needs_transform && !item.native_3d;
        const bool source_is_static = is_static || use_local;

        if (ctx.diagnostics_enabled) {
            spdlog::info(
                "[source-pass] layer='{}' kind={} item_transform_any={} implicit_center_only={} custom_transform={} use_local={} centered={} tx={} ty={}",
                layer.name.c_str(),
                static_cast<int>(layer.kind),
                item.transform.any(),
                is_implicit_2d_centering_only(item, ctx),
                has_custom_render_transform(item, ctx),
                use_local,
                should_use_centered_rendering(item, ctx),
                item.transform.position.x,
                item.transform.position.y
            );
        }

        const Mat4 item_source_world = use_local
            ? item.world_matrix
            : source_space_world_matrix(item, ctx);

        if (layer.nodes.size() == 1) {
            const auto& node = layer.nodes[0];
            const u64 content_hash = hash_render_node_content_only(node);
            const u64 placement_hash = hash_render_node_placement_only(node);
            GraphNodeId source;
            if (node.shape.type == ShapeType::Text) {
                cache::NodeCacheKey source_key{
                    .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = source_is_static ? Frame{0} : ctx.frame,
                    .width = ctx.width,
                    .height = ctx.height,
                    .params_hash = content_hash,
                    .source_hash = hash_combine(hash_string(node.name), placement_hash)
                };

                const Mat4 text_matrix = use_local
                    ? node.world_transform.to_mat4()
                    : (item_source_world * node.world_transform.to_mat4());
                const f32 text_opacity = use_local
                    ? node.world_transform.opacity
                    : (item.transform.opacity * node.world_transform.opacity);

                source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name), node, source_key,
                    should_use_centered_rendering(item, ctx),
                    item.projected,
                    ctx.modular_coordinates ? std::optional<Mat4>(text_matrix) : std::nullopt,
                    ctx.modular_coordinates ? std::optional<f32>(text_opacity) : std::nullopt,
                    source_is_static
                ));
                graph.node(source).set_frame_dependent(!source_is_static);
            } else {
                cache::NodeCacheKey source_key{
                    .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = source_is_static ? Frame{0} : ctx.frame,
                    .width = ctx.width,
                    .height = ctx.height,
                    .params_hash = content_hash,
                    .source_hash = hash_combine(hash_string(node.name), placement_hash)
                };

                const Mat4 shape_matrix = use_local
                    ? node.world_transform.to_mat4()
                    : (item_source_world * node.world_transform.to_mat4());
                const f32 shape_opacity = use_local
                    ? node.world_transform.opacity
                    : (item.transform.opacity * node.world_transform.opacity);

                source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name), node, source_key,
                    should_use_centered_rendering(item, ctx),
                    item.projected,
                    ctx.modular_coordinates ? std::optional<Mat4>(shape_matrix) : std::nullopt,
                    ctx.modular_coordinates ? std::optional<f32>(shape_opacity) : std::nullopt,
                    source_is_static
                ));
                graph.node(source).set_frame_dependent(!source_is_static);
            }
            return source;
        }

        // Build an aggregated cache key
        u64 aggregated_params_hash = 0;
        u64 aggregated_source_hash = hash_string(std::string(layer.name) + "_multisource");
        for (const auto& node : layer.nodes) {
            aggregated_params_hash = hash_combine(aggregated_params_hash, hash_render_node_content_only(node));
            aggregated_source_hash = hash_combine(
                aggregated_source_hash,
                hash_combine(hash_string(node.name), hash_render_node_placement_only(node))
            );
        }

        cache::NodeCacheKey source_key{
            .scope = "layer.multisource:" + std::string(layer.name),
            .frame = source_is_static ? Frame{0} : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = aggregated_params_hash,
            .source_hash = aggregated_source_hash
        };

        std::vector<MultiSourceItem> items;
        items.reserve(layer.nodes.size());

        for (const auto& node : layer.nodes) {
            const Mat4 shape_matrix = use_local
                ? node.world_transform.to_mat4()
                : (item_source_world * node.world_transform.to_mat4());
            const f32 shape_opacity = use_local
                ? node.world_transform.opacity
                : (item.transform.opacity * node.world_transform.opacity);

            items.push_back(MultiSourceItem{
                .node = &node,
                .matrix = shape_matrix,
                .opacity = shape_opacity
            });
        }

        auto multi_source = graph.add_node(std::make_unique<MultiSourceNode>(
            std::string(layer.name) + "_multi",
            std::move(items),
            source_key,
            should_use_centered_rendering(item, ctx),
            item.projected,
            source_is_static
        ));
        graph.node(multi_source).set_frame_dependent(!source_is_static);
        return multi_source;
    }

    if (layer.kind == LayerKind::Precomp) {
        auto precomp_id = graph.add_node(std::make_unique<PrecompNode>(
            std::string(layer.precomp_composition_name), layer.from, layer.duration,
            is_static ? Frame{0} : Frame{-1}
        ));
        graph.node(precomp_id).set_frame_dependent(!is_static);
        return precomp_id;
    }

    if (layer.kind == LayerKind::Video && layer.video_source) {
        auto video_id = graph.add_node(std::make_unique<VideoNode>(
            *layer.video_source, ctx.video_decoder, layer.from
        ));
        graph.node(video_id).set_frame_dependent(true);
        return video_id;
    }

    return graph.add_node(std::make_unique<ClearNode>());
}

} // namespace chronon3d::graph::detail
