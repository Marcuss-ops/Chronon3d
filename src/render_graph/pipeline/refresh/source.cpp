#include "source.hpp"
#include "layer_item.hpp"

#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/graph_builder_internal.hpp"

namespace chronon3d::graph::detail {

void refresh_source_node(
    SourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, const RenderNode*>& root_nodes_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx)
{
    const std::string layer_id{node.layer_id()};

    // ── Case 1: Root-level source (no layer) ──────────────────────────
    if (layer_id.empty()) {
        const auto it = root_nodes_by_name.find(std::string{node.name()});
        if (it == root_nodes_by_name.end()) return;
        const RenderNode& src_node = *it->second;
        cache::NodeCacheKey key{
            .scope = "root.source:" + std::string(src_node.name),
            .frame = ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_render_node(src_node),
            .source_hash = hash_bytes(src_node.name.data(), src_node.name.size())
        };
        node.refresh(
            std::string(src_node.name),
            src_node,
            key,
            ctx.policy.modular_coordinates
        );
        return;
    }

    // ── Case 2: Layer-level source ────────────────────────────────────
    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const ResolvedLayer& rl = *layer_it->second;
    const Layer& layer = *rl.layer;
    if (layer.kind != LayerKind::Normal || layer.nodes.size() != 1) {
        return;
    }

    const auto& src_node = layer.nodes[0];
    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
    const bool use_local = ctx.policy.modular_coordinates &&
        layer_needs_render_transform(item, ctx) &&
        !item.native_3d;
    const std::string layer_name_str(layer.name);
    const bool item_static = is_static_cache.count(layer_name_str)
        ? is_static_cache.at(layer_name_str) : layer.cache_static;
    const bool source_is_static = item_static || use_local;
    const Mat4 item_source_world = use_local
        ? item.world_matrix
        : source_space_world_matrix(item, ctx);
    const Mat4 node_matrix = src_node.world_transform.to_mat4();
    const Mat4 render_matrix = use_local
        ? node_matrix
        : (item_source_world * node_matrix);
    const f32 render_opacity = use_local
        ? src_node.world_transform.opacity
        : (item.transform.opacity * src_node.world_transform.opacity);
    cache::NodeCacheKey key{
        .scope = "layer.source:" + layer_name_str + ":" + std::string(src_node.name),
        .frame = source_is_static ? Frame{0} : ctx.frame_input.frame,
        .width = ctx.frame_input.width,
        .height = ctx.frame_input.height,
        .params_hash = hash_render_node(src_node),
        .source_hash = hash_bytes(src_node.name.data(), src_node.name.size())
    };

    node.refresh(
        std::string(src_node.name),
        src_node,
        key,
        should_use_centered_rendering(item, ctx),
        item.projected,
        ctx.policy.modular_coordinates ? std::optional<Mat4>(render_matrix) : std::nullopt,
        ctx.policy.modular_coordinates ? std::optional<f32>(render_opacity) : std::nullopt,
        source_is_static ? static_memory_cache("source") : frame_variant_cache("source")
    );
}

} // namespace chronon3d::graph::detail
