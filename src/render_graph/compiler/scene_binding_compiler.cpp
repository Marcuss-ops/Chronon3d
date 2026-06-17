// =============================================================================
// scene_binding_compiler.cpp — B5: Binding table construction
//
/// Walks the optimized (compiled) graph and builds the binding table.
/// Reads SceneBindingMetadata from compiled.nodes[].binding_meta (which
/// was set during graph construction, and preserved/merged by the optimizer).
///
/// The compiler:
///   1. Iterates all graph nodes in topological order (by execution level).
///   2. Skips nodes where binding_meta.has_binding() is false (internal
///      nodes like Clear, Output, etc.).
///   3. For effect stack nodes, merges contiguous effect ranges if the
///      optimizer fused multiple layers' effects.
///   4. Sets the kind based on the node RenderGraphNodeKind.
// =============================================================================

#include <chronon3d/render_graph/compiler/scene_binding.hpp>

namespace chronon3d::graph {

/// Convert a RenderGraphNodeKind to the corresponding SceneBindingKind.
static SceneBindingKind node_kind_to_binding_kind(RenderGraphNodeKind kind) {
    switch (kind) {
        case RenderGraphNodeKind::Source:      return SceneBindingKind::Source;
        case RenderGraphNodeKind::TextRun:     return SceneBindingKind::Source; // PR 3: TextRun is source-shaped
        case RenderGraphNodeKind::Transform:   return SceneBindingKind::Transform;
        case RenderGraphNodeKind::Effect:      return SceneBindingKind::EffectStack;
        case RenderGraphNodeKind::TrackMatte:  return SceneBindingKind::TrackMatte;
        case RenderGraphNodeKind::Transition:  return SceneBindingKind::Transition;
        case RenderGraphNodeKind::Adjustment:  return SceneBindingKind::Adjustment;
        default:                               return SceneBindingKind::Source;
    }
}

std::vector<SceneBinding> build_binding_table(
    const CompiledFrameGraph& compiled)
{
    std::vector<SceneBinding> bindings;
    bindings.reserve(compiled.nodes.size() / 2);  // rough estimate

    // Iterate in topological order (execution levels) so bindings appear
    // in the same order the executor processes them.
    for (const auto& level : compiled.levels) {
        for (GraphNodeId node_id : level) {
            if (node_id >= compiled.nodes.size()) continue;
            const auto& node_info = compiled.nodes[node_id];
            if (!node_info.reachable) continue;
            if (!node_info.binding_meta.has_binding()) continue;

            const auto& meta = node_info.binding_meta;
            SceneBindingKind kind = node_kind_to_binding_kind(node_info.kind);

            // For effect nodes, check if the optimizer merged multiple ranges.
            // If this effect range is adjacent to the last entry for the same
            // layer, extend the range instead of adding a new binding.
            if (kind == SceneBindingKind::EffectStack && !bindings.empty()) {
                auto& last = bindings.back();
                if (last.kind == SceneBindingKind::EffectStack &&
                    last.layer_index == meta.layer_index &&
                    last.effect_begin + last.effect_count == meta.effect_begin) {
                    // Merge contiguous effect ranges.
                    last.effect_count += meta.effect_count;
                    // Keep the last node_id for this effect group (the one
                    // that actually processes all effects).
                    last.node_id = node_id;
                    continue;
                }
            }

            bindings.push_back(SceneBinding{
                .kind          = kind,
                .node_id       = node_id,
                .layer_index   = meta.layer_index,
                .item_index    = meta.item_index,
                .effect_begin  = meta.effect_begin,
                .effect_count  = meta.effect_count
            });
        }
    }

    return bindings;
}

/// Convenience: build a CompiledSceneProgram from a CompiledFrameGraph.
/// Takes ownership of the frame graph and appends the binding table.
CompiledSceneProgram compile_scene_program(
    CompiledFrameGraph frame_graph)
{
    CompiledSceneProgram program;
    program.frame_graph = std::move(frame_graph);
    program.bindings    = build_binding_table(program.frame_graph);
    program.valid       = program.frame_graph.valid;
    return program;
}

} // namespace chronon3d::graph
