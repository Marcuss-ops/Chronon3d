#pragma once

// ---------------------------------------------------------------------------
// scene_refresh.hpp
//
// Refreshes compiled graph payloads (SourceNode, MultiSourceNode,
// EffectStackNode, TransformNode) when reusing a cached compiled graph
// across frames.
//
// Header-only (inline) — extracted from scene.cpp to keep that file
// focused on orchestration.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "../builder/graph_builder_bbox.hpp"
#include "../builder/graph_builder_coordinates.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include <algorithm>
#include <unordered_map>
#include <string>

namespace chronon3d::graph::detail {

[[nodiscard]] static inline LayerGraphItem make_layer_graph_item_for_refresh(
    const ResolvedLayer& resolved_layer,
    const RenderGraphContext& ctx
) {
    const Layer& layer = *resolved_layer.layer;

    if (ctx.camera.camera.camera_2_5d.enabled && layer.uses_2_5d_projection) {
        Transform effective_transform = resolved_layer.world_transform;
        const Mat4 projection_world_matrix = effective_transform.to_mat4();
        auto proj = project_layer_2_5d(
            effective_transform,
            projection_world_matrix,
            ctx.camera.camera.camera_2_5d,
            static_cast<f32>(ctx.frame.frame.width),
            static_cast<f32>(ctx.frame.frame.height),
            ctx.options.diagnostics_enabled
        );
        if (proj.visible) {
            const Mat4 eff_proj = is_native_3d_layer(layer)
                ? Mat4(1.0f)
                : proj.projection_matrix;
            return LayerGraphItem{
                .layer             = resolved_layer.layer,
                .transform         = proj.transform,
                .world_matrix      = resolved_layer.world_matrix,
                .projection_matrix = eff_proj,
                .depth             = proj.depth,
                .world_z           = resolved_layer.world_transform.position.z,
                .projected         = true,
                .native_3d         = is_native_3d_layer(layer),
                .insertion_index   = resolved_layer.insertion_index,
                .matte_node        = k_invalid_node,
                // NOTE: .is_static intentionally left at default — the refresh
                // path uses is_static_cache (recursive analysis) instead.
            };
        }
    }

    return LayerGraphItem{
        .layer           = resolved_layer.layer,
        .transform       = resolved_layer.world_transform,
        .world_matrix    = resolved_layer.world_matrix,
        .depth           = 0.0f,
        .world_z         = resolved_layer.world_transform.position.z,
        .projected       = false,
        .native_3d       = is_native_3d_layer(layer),
        .insertion_index = resolved_layer.insertion_index,
        .matte_node      = k_invalid_node,
        // NOTE: .is_static intentionally left at default — see above.
    };
}

/// Re-populate all node payloads in a compiled graph with fresh scene data.
/// Called when reusing a cached compiled graph from the previous frame.
inline void refresh_compiled_graph_payloads(
    CompiledFrameGraph& compiled,
    const Scene& scene,
    RenderGraphContext& ctx,
    const LayerResolutionResult& resolved
) {
    // Compute recursive static analysis — must match the builder path
    // (graph_builder_source_pass.cpp) which uses item.is_static from this
    // cache.  Without this, source_is_static uses only layer.cache_static,
    // missing parent/transition/animated propagation.
    std::unordered_map<std::string, bool> is_static_cache;
    compute_static_layers(resolved, is_static_cache);

    std::unordered_map<std::string, const ResolvedLayer*> resolved_by_name;
    resolved_by_name.reserve(resolved.layers.size());
    for (const auto& rl : resolved.layers) {
        if (rl.layer) {
            resolved_by_name.emplace(std::string(rl.layer->name), &rl);
        }
    }

    std::unordered_map<std::string, const RenderNode*> root_nodes_by_name;
    root_nodes_by_name.reserve(scene.nodes().size());
    for (const auto& node : scene.nodes()) {
        root_nodes_by_name.emplace(std::string(node.name), &node);
    }

    const auto refresh_source_node = [&](SourceNode& node) {
        const std::string layer_id = node.layer_id();
        if (layer_id.empty()) {
            const auto it = root_nodes_by_name.find(node.name());
            if (it == root_nodes_by_name.end()) return;
            const RenderNode& src_node = *it->second;
            cache::NodeCacheKey key{
                .scope = "root.source:" + std::string(src_node.name),
                .frame = ctx.frame.frame.frame,
                .width = ctx.frame.frame.width,
                .height = ctx.frame.frame.height,
                .params_hash = hash_render_node(src_node),
                .source_hash = hash_bytes(src_node.name.data(), src_node.name.size())
            };
            node.refresh(
                std::string(src_node.name),
                src_node,
                key,
                ctx.options.modular_coordinates
            );
            return;
        }

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
        const Mat4 node_matrix = src_node.world_transform.to_mat4();
        const Mat4 render_matrix = use_local
            ? node_matrix
            : (item_source_world * node_matrix);
        const f32 render_opacity = use_local
            ? src_node.world_transform.opacity
            : (item.transform.opacity * src_node.world_transform.opacity);
        cache::NodeCacheKey key{
            .scope = "layer.source:" + layer_name_str + ":" + std::string(src_node.name),
            .frame = source_is_static ? Frame{0} : ctx.frame.frame.frame,
            .width = ctx.frame.frame.width,
            .height = ctx.frame.frame.height,
            .params_hash = hash_render_node(src_node),
            .source_hash = hash_bytes(src_node.name.data(), src_node.name.size())
        };

        node.refresh(
            std::string(src_node.name),
            src_node,
            key,
            should_use_centered_rendering(item, ctx),
            item.projected,
            ctx.options.modular_coordinates ? std::optional<Mat4>(render_matrix) : std::nullopt,
            ctx.options.modular_coordinates ? std::optional<f32>(render_opacity) : std::nullopt,
            source_is_static
        );
    };

    const auto refresh_multi_source_node = [&](MultiSourceNode& node) {
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
            .frame = source_is_static ? Frame{0} : ctx.frame.frame.frame,
            .width = ctx.frame.frame.width,
            .height = ctx.frame.frame.height,
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
    };

    const auto refresh_effect_stack_node = [&](EffectStackNode& node) {
        const std::string layer_id = node.layer_id();
        if (layer_id.empty()) return;
        const auto layer_it = resolved_by_name.find(layer_id);
        if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
            return;
        }
        const Layer& layer = *layer_it->second->layer;
        if (layer.anim_transform.blur.is_animated()) {
            const Frame local_frame = layer.local_frame(ctx.frame.frame.frame);
            const f32 blur_radius = layer.anim_transform.blur.evaluate(local_frame);
            for (auto& effect : node.effects()) {
                if (auto* blur = std::get_if<BlurParams>(&effect.params)) {
                    blur->radius = blur_radius;
                }
            }
        }
    };

    const auto refresh_transform_node = [&](TransformNode& node) {
        const std::string layer_id = node.layer_id();
        if (layer_id.empty()) return;

        const auto layer_it = resolved_by_name.find(layer_id);
        if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
            return;
        }

        const ResolvedLayer& rl = *layer_it->second;
        const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);

        if (item.projected) {
            node.set_matrix(item.projection_matrix);
            node.set_opacity(item.transform.opacity);
        } else {
            Mat4 effective_matrix = item.world_matrix;
            if (should_use_centered_rendering(item, ctx)) {
                if (ctx.options.ssaa_factor > 1.0f) {
                    Mat4 ssaa_world = item.world_matrix;
                    ssaa_world[3][0] *= ctx.options.ssaa_factor;
                    ssaa_world[3][1] *= ctx.options.ssaa_factor;
                    ssaa_world[3][2] *= ctx.options.ssaa_factor;
                    effective_matrix = ssaa_world;
                }
                effective_matrix =
                    glm::translate(Mat4(1.0f), Vec3(-ctx.frame.frame.width * 0.5f, -ctx.frame.frame.height * 0.5f, 0.0f)) *
                    effective_matrix;
            } else {
                // Delegate to the shared helper so the refresh-path stays in
                // sync with the build-path (graph_builder_layer_passes.cpp).
                const Mat4 stripped = strip_implicit_canvas_centering(
                    effective_matrix, item, ctx);
                if (ctx.options.diagnostics_enabled &&
                    (stripped[3][0] != effective_matrix[3][0] ||
                     stripped[3][1] != effective_matrix[3][1])) {
                    spdlog::info("[refresh-transform] stripped centering translation for layer='{}' frame={}",
                        layer_id, static_cast<int>(ctx.frame.frame.frame));
                }
                effective_matrix = stripped;
            }
            node.set_matrix(effective_matrix);
            node.set_opacity(item.transform.opacity);
        }
    };

    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(static_cast<GraphNodeId>(id))) {
            continue;
        }

        auto& graph_node = compiled.graph.node(static_cast<GraphNodeId>(id));
        // Dispatch via kind() to avoid sequential dynamic_cast RTTI lookups.
        // SourceNode and MultiSourceNode both report Source kind, so the
        // Source case still needs a single dynamic_cast to disambiguate.
        switch (graph_node.kind()) {
            case RenderGraphNodeKind::Source:
                if (auto* source_node = dynamic_cast<SourceNode*>(&graph_node)) {
                    refresh_source_node(*source_node);
                } else if (auto* multi = dynamic_cast<MultiSourceNode*>(&graph_node)) {
                    refresh_multi_source_node(*multi);
                }
                break;
            case RenderGraphNodeKind::Effect:
                refresh_effect_stack_node(static_cast<EffectStackNode&>(graph_node));
                break;
            case RenderGraphNodeKind::Transform:
                refresh_transform_node(static_cast<TransformNode&>(graph_node));
                break;
            default:
                break;
        }
    }
}

} // namespace chronon3d::graph::detail
