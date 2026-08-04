#pragma once

// ============================================================================
// scene_refresh.hpp — Public API for refreshing compiled graph payloads.
//
// Re-populates all node payloads in a compiled graph with fresh scene data
// when reusing a cached compiled graph across frames.
//
// Dispatches to per-node-type refreshers:
//   - SourceNode       → refresh/source.cpp
//   - MultiSourceNode  → refresh/multi_source.cpp
//   - EffectStackNode  → refresh/effect_stack.cpp
//   - TransformNode    → refresh/transform.cpp
// ============================================================================

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <string>

namespace chronon3d {
    class Scene;
}

namespace chronon3d::graph::detail {

enum class SceneRefreshStatus {
    Refreshed,
    TopologyMismatch,
    InvalidRenderableNode,
    MissingDynamicData,
};

struct SceneRefreshResult {
    SceneRefreshStatus status{SceneRefreshStatus::Refreshed};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == SceneRefreshStatus::Refreshed;
    }
};

/// Validate the complete compiled-node structure, then refresh only dynamic
/// payloads. Validation runs before any node is mutated. The caller owns the
/// detached candidate graph, so a failure is never published as a partially
/// refreshed cache entry.
[[nodiscard]] SceneRefreshResult refresh_compiled_graph_payloads(
    CompiledFrameGraph& compiled,
    const Scene& scene,
    RenderGraphContext& ctx,
    const LayerResolutionResult& resolved);

} // namespace chronon3d::graph::detail
