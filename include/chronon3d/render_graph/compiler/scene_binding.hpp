#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// scene_binding.hpp — B5: Scene binding types for the graph compiler
//
// Two concepts are introduced here:
//   1. SceneBinding — a finalized entry in the binding table (from
//      compiled_scene_program.hpp, included transitively via compiled_frame_graph).
//   2. PendingSceneBinding — pre-compiler binding descriptor emitted by the
//      graph builder.  The binding compiler converts these into final
//      SceneBinding entries after the optimizer has run.
//
// The SceneBindingMetadata struct itself lives in compiled_frame_graph.hpp
// as a field of CompiledNodeInfo, because that's where it's ultimately stored.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <string>
#include <vector>

namespace chronon3d::graph {

/// Retrieve binding metadata from compiled node info.  Returns a default
/// (empty) metadata if the node has no binding info set.
inline const SceneBindingMetadata& get_binding_metadata(const CompiledNodeInfo& info) {
    return info.binding_meta;
}

inline SceneBindingMetadata& get_binding_metadata(CompiledNodeInfo& info) {
    return info.binding_meta;
}

inline void set_binding_metadata(CompiledNodeInfo& info, const SceneBindingMetadata& meta) {
    info.binding_meta = meta;
}

// ── Pending binding (before compilation) ─────────────────────────────────────
//
/// Emitted by the graph builder for each binding-relevant node.
/// The binding compiler later converts these into SceneBinding entries
/// after the optimizer has run (so GraphNodeId values are stable).
struct PendingSceneBinding {
    SceneBindingKind kind{SceneBindingKind::Source};
    GraphNodeId      node_id{k_invalid_node};

    uint32_t layer_index{0};
    uint32_t item_index{0};

    uint16_t effect_begin{0};
    uint16_t effect_count{0};
};

// ── GraphBuildArtifacts ───────────────────────────────────────────────────────
//
/// Extended build result that includes the pending binding table.
/// The new `build_artifacts_with_resolved()` method returns this instead
/// of a bare RenderGraph.
struct GraphBuildArtifacts {
    RenderGraph                    graph;
    std::vector<PendingSceneBinding> bindings;
};

/// Free function: build the binding table from a CompiledFrameGraph.
/// Scans compiled.nodes[] for SceneBindingMetadata entries and produces
/// the ordered SceneBinding list.  Defined in scene_binding_compiler.cpp.
[[nodiscard]] std::vector<SceneBinding> build_binding_table(
    const CompiledFrameGraph& compiled);

/// Convenience: build a CompiledSceneProgram from a CompiledFrameGraph.
[[nodiscard]] CompiledSceneProgram compile_scene_program(
    CompiledFrameGraph frame_graph);

} // namespace chronon3d::graph
