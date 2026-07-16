// =============================================================================
// scene_program_refresh.cpp — B5: Compiled scene program refresh
//
/// Refreshes all per-frame payloads in a CompiledSceneProgram by walking the
/// binding table.  This replaces the previous approach of scanning all graph
/// nodes with per-name unordered_map lookups.
///
/// Key differences from the old refresh_compiled_graph_payloads():
///   1. No per-name resolved_by_name map — uses direct layer_index access.
///   2. No per-name root_nodes_by_name map — root sources use a special
///      binding with layer_index == UINT32_MAX.
///   3. No dynamic_cast for Source vs MultiSource — kind is in the binding.
///   4. No scanning of non-binding nodes (clear nodes, output, etc.).
///
/// The refresh dispatches to the same per-node-type refreshers as before
/// (refresh/source.cpp, refresh/transform.cpp, refresh/effect_stack.cpp),
/// but with direct layer pointers instead of name lookups.
// =============================================================================

#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <chronon3d/render_graph/compiler/scene_binding.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../builder/graph_builder_coordinates.hpp"
#include "refresh/layer_item.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

// Forward declarations of the per-node-type refreshers from existing pipeline.
// These are defined in refresh/ subdirectory.
namespace chronon3d::graph::detail {
void refresh_source_node(
    SourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, const RenderNode*>& root_nodes_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx);

void refresh_multi_source_node(
    MultiSourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx);

void refresh_effect_stack_node(
    EffectStackNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx);

void refresh_transform_node(
    TransformNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx);
} // namespace chronon3d::graph::detail

namespace chronon3d::graph {

// ── Root source index sentinel ───────────────────────────────────────────────
// When layer_index is this value, the binding refers to a root-level
// (non-layer) source node.
static constexpr uint32_t kRootSourceIndex = UINT32_MAX;

void refresh_compiled_scene_program(
    CompiledSceneProgram& program,
    const Scene& scene,
    RenderGraphContext& ctx,
    const detail::LayerResolutionResult& resolved)
{
    if (!program.valid || program.bindings.empty()) return;

    // ── 1. Build per-layer resolved lookup (compact — no name hashing) ───
    // Since we index by layer_index directly, this is O(1) per binding.
    const auto& layers = resolved.layers;

    // ── 2. Build root node lookup (only needed for root sources) ──────────
    // This is the same as before but only populated when actually needed.
    std::unordered_map<std::string, const RenderNode*> root_nodes_by_name;
    bool need_root_lookup = false;
    for (const auto& binding : program.bindings) {
        if (binding.layer_index == kRootSourceIndex) {
            need_root_lookup = true;
            break;
        }
    }
    if (need_root_lookup) {
        root_nodes_by_name.reserve(scene.nodes().size());
        for (const auto& node : scene.nodes()) {
            root_nodes_by_name.emplace(std::string(node.name), &node);
        }
    }

    // ── 3. Build is_static_cache (same as before but only for needed layers) ─
    // TODO: This still uses the old compute_static_layers. In a future
    // refactoring (B6), the is_static information could be stored in the
    // binding itself to avoid this rebuild.
    std::unordered_map<std::string, bool> is_static_cache;
    {
        std::unordered_set<std::string> needed_layers;
        for (const auto& binding : program.bindings) {
            if (binding.layer_index < layers.size() && layers[binding.layer_index].layer) {
                needed_layers.emplace(std::string(layers[binding.layer_index].layer->name));
            }
        }
        // Compute static analysis only for layers that have bindings.
        // This is called via the existing compute_static_layers function.
        // For now, use a simple heuristic: layer is static if cache_static is set.
        for (const auto& name : needed_layers) {
            for (const auto& rl : layers) {
                if (rl.layer && std::string_view(rl.layer->name) == name) {
                    is_static_cache[name] = rl.layer->cache_static;
                    break;
                }
            }
        }
    }

    // ── 4. Build resolved_by_name lookup (only needed for legacy refreshers) ─
    // The existing refresh_* functions still expect name-based lookups.
    // We build a minimal map of only the layers we actually need.
    std::unordered_map<std::string, const ResolvedLayer*> resolved_by_name;
    resolved_by_name.reserve(program.bindings.size());
    for (const auto& binding : program.bindings) {
        if (binding.layer_index < layers.size() && layers[binding.layer_index].layer) {
            const auto& rl = layers[binding.layer_index];
            resolved_by_name.emplace(std::string(rl.layer->name), &rl);
        }
    }

    // ── 5. Walk bindings and dispatch ─────────────────────────────────────
    for (const SceneBinding& binding : program.bindings) {
        if (!program.frame_graph.graph.has_node(binding.node_id)) continue;
        auto& graph_node = program.frame_graph.graph.node(binding.node_id);

        switch (binding.kind) {
            case SceneBindingKind::Transform: {
                auto* node = dynamic_cast<TransformNode*>(&graph_node);
                if (node) {
                    detail::refresh_transform_node(*node, resolved_by_name, ctx);
                }
                break;
            }

            case SceneBindingKind::Source: {
                auto* src = dynamic_cast<SourceNode*>(&graph_node);
                if (src) {
                    detail::refresh_source_node(*src, resolved_by_name,
                        root_nodes_by_name, is_static_cache, ctx);
                } else if (auto* multi = dynamic_cast<MultiSourceNode*>(&graph_node)) {
                    detail::refresh_multi_source_node(*multi,
                        resolved_by_name, is_static_cache, ctx);
                } else if (auto* text = dynamic_cast<TextRunNode*>(&graph_node)) {
                    if (binding.layer_index < layers.size()) {
                        const auto& rl = layers[binding.layer_index];
                        if (rl.layer && rl.layer->kind == LayerKind::Text
                            && rl.layer->nodes.size() == 1) {
                            const auto& render_ref = rl.layer->nodes[0];
                            const auto item = detail::make_layer_graph_item_for_refresh(rl, ctx);
                            f32 opacity = 1.0f;
                            const auto placement = detail::resolve_text_run_placement(
                                item, render_ref, ctx, opacity);
                            cache::NodeCacheKey key{
                                .scope = "layer.textrun:" + std::string(rl.layer->name)
                                    + ":" + std::string(render_ref.name),
                                .frame = ctx.frame_input.frame,
                                .width = ctx.frame_input.width,
                                .height = ctx.frame_input.height,
                                .params_hash = hash_render_node_content_only(render_ref),
                                .source_hash = hash_combine(
                                    hash_string(render_ref.name),
                                    hash_render_node_placement_only(render_ref))
                            };
                            text->refresh_placement(
                                render_ref, placement,
                                key, std::optional<f32>(opacity));
                        }
                    }
                }
                break;
            }

            case SceneBindingKind::MultiSource: {
                auto* multi = dynamic_cast<MultiSourceNode*>(&graph_node);
                if (multi) {
                    detail::refresh_multi_source_node(*multi,
                        resolved_by_name, is_static_cache, ctx);
                }
                break;
            }

            case SceneBindingKind::EffectStack: {
                auto* node = dynamic_cast<EffectStackNode*>(&graph_node);
                if (node) {
                    detail::refresh_effect_stack_node(*node, resolved_by_name, ctx);
                }
                break;
            }

            default:
                // TrackMatte, Transition, Adjustment — no per-frame payload refresh needed
                break;
        }
    }
}

} // namespace chronon3d::graph
