#pragma once

// ---------------------------------------------------------------------------
// graph_cache_coordinator.hpp
//
// Coordinates the decision to reuse a cached compiled graph from the
// previous frame vs. building a fresh graph.  When reusing, refreshes
// node payloads with current frame data (source content, transforms,
// effect parameters).
// ===========================================================================

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "scene_internal.hpp"
#include "scene_fingerprint.hpp"
#include <memory>

namespace chronon3d::graph {

/// Result of the graph build-or-reuse decision.
struct GraphBuildResult {
    CompiledFrameGraph compiled;
    bool graph_reused{false};
    bool can_reuse{false};

    // State carried forward for downstream consumers
    bool skip_initial_clear{false};
};

/// Decide whether the compiled graph can be reused and either reuse it
/// (refreshing payloads) or build from scratch.
///
/// When `scene_structure_unchanged` is true and a cached graph exists
/// for the current resolution, the graph structure is identical to the
/// previous frame — only payloads need refreshing.
///
/// Otherwise, performs a full build via GraphBuildPipeline + FrameGraphCompiler.
[[nodiscard]] GraphBuildResult build_or_reuse_graph(
    RenderGraphContext& ctx,
    const Scene& scene,
    const detail::LayerResolutionResult& resolved,
    SoftwareRenderer* sw_renderer,
    int width,
    int height,
    bool scene_structure_unchanged,
    bool diagnostics_enabled);

} // namespace chronon3d::graph
