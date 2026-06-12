#pragma once

// ---------------------------------------------------------------------------
// scene_refresh.hpp
//
// Public API for refreshing compiled graph payloads when reusing a cached
// compiled graph across frames.
//
// Implementation is in scene_refresh.cpp and refresh/ subdirectory.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "../builder/graph_builder_internal.hpp"

namespace chronon3d::graph::detail {

/// Re-populate all node payloads in a compiled graph with fresh scene data.
/// Called when reusing a cached compiled graph from the previous frame.
///
/// Dispatches to per-node-type refreshers:
///   - SourceNode       → refresh/source.cpp
///   - MultiSourceNode  → refresh/multi_source.cpp
///   - EffectStackNode  → refresh/effect_stack.cpp
///   - TransformNode    → refresh/transform.cpp
void refresh_compiled_graph_payloads(
    CompiledFrameGraph& compiled,
    const Scene& scene,
    RenderGraphContext& ctx,
    const LayerResolutionResult& resolved);

} // namespace chronon3d::graph::detail
