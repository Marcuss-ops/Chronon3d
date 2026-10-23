#include "source.hpp"
#include "layer_item.hpp"

#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/geometry/mesh.hpp>

#include <cmath>
#include "../../builder/graph_builder_coordinates.hpp"
#include "../../builder/evaluated_layer_placement.hpp"
#include "../../builder/graph_builder_internal.hpp"
#include "../../builder/passes/graph_builder_source_pass.hpp"

namespace chronon3d::graph::detail {

void refresh_source_node(
    SourceNode& node,
    const std::unordered_map<std::string_view, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string_view, const RenderNode*>& root_nodes_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx,
    const compiler::SampledMeshParameters* sampled_mesh)
{
    const std::string layer_id{node.layer_id()};

    // ── Case 1: Root-level source (no layer) ──────────────────────────
    if (layer_id.empty()) {
        const auto it = root_nodes_by_name.find(node.name());
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

    auto src_node = materialize_mesh_node(layer.nodes[0], ctx);
    const bool has_mesh_sample = sampled_mesh && sampled_mesh->inline_mesh &&
        sampled_mesh->animation_before && sampled_mesh->animation_after;
    if (has_mesh_sample) {
        auto& shape = src_node.shape.mesh_shape();
        const auto& base_mesh = shape.inline_mesh;
        const auto& before = *sampled_mesh->animation_before;
        const auto& after = *sampled_mesh->animation_after;
        if (!base_mesh || before.positions.size() != base_mesh->vertices().size() ||
            after.positions.size() != base_mesh->vertices().size())
            throw std::runtime_error("animated inline mesh sample does not match source topology");
        std::vector<Vec3> positions;
        std::vector<Vec3> normals(base_mesh->vertices().size(), Vec3{0.0f});
        positions.reserve(base_mesh->vertices().size());
        for (std::size_t i = 0; i < base_mesh->vertices().size(); ++i) {
            const auto& a = before.positions[i];
            const auto& b = after.positions[i];
            positions.emplace_back(
                a[0] + (b[0] - a[0]) * sampled_mesh->animation_mix,
                a[1] + (b[1] - a[1]) * sampled_mesh->animation_mix,
                a[2] + (b[2] - a[2]) * sampled_mesh->animation_mix);
        }
        const auto& indices = base_mesh->indices();
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= positions.size() || b >= positions.size() || c >= positions.size())
                throw std::runtime_error("animated inline mesh contains an out-of-range index");
            const Vec3 face = glm::cross(positions[b] - positions[a], positions[c] - positions[a]);
            normals[a] += face; normals[b] += face; normals[c] += face;
        }
        auto deformed = std::make_shared<Mesh>(base_mesh->name());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            Vec3 normal = glm::dot(normals[i], normals[i]) > 1e-12f
                ? glm::normalize(normals[i]) : Vec3{0.0f, 0.0f, 1.0f};
            const auto& source_vertex = base_mesh->vertices()[i];
            deformed->add_vertex(Vertex{positions[i], normal, source_vertex.uv});
        }
        for (const auto index : indices) deformed->add_index(index);
        shape.inline_mesh = std::move(deformed);
    }
    const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
    const auto source_placement = evaluate_source_placement(item, src_node, ctx);
    const std::string layer_name_str(layer.name);
    // AUDIT F12 — one hash per layer instead of two: the previous
    // count()-then-at() pair hashed the layer name twice per refresh; a
    // single find() hashes once and keeps the fallback bit-identical.
    const auto static_it = is_static_cache.find(layer_name_str);
    const bool item_static = static_it != is_static_cache.end()
        ? static_it->second : layer.cache_static;
    const bool source_is_static = item_static && !has_mesh_sample;
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
