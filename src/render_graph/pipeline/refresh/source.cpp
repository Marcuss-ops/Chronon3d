#include "source.hpp"
#include "layer_item.hpp"

#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/evaluated_layer_placement.hpp"
#include "../../builder/graph_builder_internal.hpp"
#include "../../builder/passes/graph_builder_source_pass.hpp"

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
        const RenderNode src_node = materialize_mesh_node(*it->second, ctx);
        // Canonical cache identity: the builder folds camera state by
        // construction (TICKET-ae-cam-hash-collision Soluzione B) — root
        // source keys differentiate per-camera-state (AE_CAM_02 zoom-only).
        cache::NodeCacheKey key = cache::NodeCacheIdentityBuilder{
            "root.source:" + std::string(src_node.name)
        }
            .frame(ctx.frame_input.frame)
            .output(ctx.frame_input.width, ctx.frame_input.height)
            .params(hash_render_node(src_node))
            .source(hash_bytes(src_node.name.data(), src_node.name.size()))
            .camera_if(ctx.frame_input.has_camera_2_5d,
                       ctx.frame_input.camera_2_5d)
            .build();
        // Keep refresh byte-equivalent to append_root_sources() through the
        // canonical root-source placement helper.
        const auto matrix_override = root_source_matrix_override(src_node, ctx);
        node.refresh(
            std::string(src_node.name),
            src_node,
            key,
            matrix_override,
            std::optional<f32>(src_node.world_transform.opacity),
            // Root sources have no layer-level static analysis. Their
            // authored payload can vary with the current frame, so refresh
            // must keep the compiled node frame-variant just like the fresh
            // builder path.
            node.cache_policy()
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
    const bool source_layer_kind =
        layer.kind == LayerKind::Normal ||
        layer.kind == LayerKind::Shape ||
        layer.kind == LayerKind::Text;
    if (!source_layer_kind || layer.nodes.size() != 1) {
        return;
    }

    const auto src_node = materialize_mesh_node(layer.nodes[0], ctx);
    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
    const auto source_placement = evaluate_source_placement(item, src_node, ctx);
    const std::string layer_name_str(layer.name);
    const bool item_static = is_static_cache.count(layer_name_str)
        ? is_static_cache.at(layer_name_str) : layer.cache_static;
    const bool source_is_static = item_static;
    const Mat4 render_matrix = finalize_source_placement_matrix(
        source_placement, item, src_node, ctx);
    const f32 render_opacity = source_placement.opacity;
    // Canonical cache identity (TICKET-ae-cam-hash-collision Soluzione B):
    // the builder folds the evaluated camera state by construction so
    // zoom-animated / Z-dolly frames never collide at the cache-key level.
    cache::NodeCacheKey key = cache::NodeCacheIdentityBuilder{
        "layer.source:" + layer_name_str + ":" + std::string(src_node.name)
    }
        .frame(source_is_static ? Frame{0} : ctx.frame_input.frame)
        .output(ctx.frame_input.width, ctx.frame_input.height)
        .params(hash_render_node(src_node))
        .source(hash_bytes(src_node.name.data(), src_node.name.size()))
        .camera_if(ctx.frame_input.has_camera_2_5d,
                   ctx.frame_input.camera_2_5d)
        .build();

    node.refresh(
        std::string(src_node.name),
        src_node,
        key,
        std::optional<Mat4>(render_matrix),
        std::optional<f32>(render_opacity),
        node.cache_policy(),
        source_placement.layer.defer_camera_projection,
        item.native_3d
    );
}

} // namespace chronon3d::graph::detail
