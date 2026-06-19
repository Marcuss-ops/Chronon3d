#pragma once

// ============================================================================
// layer_resolver.hpp — Public API for resolving scene layers into flat lists.
//
// Extracted from graph_builder_internal.hpp so that pipeline code
// (precomp_node_execute.cpp, scene.cpp, tests) can include it cleanly
// without pulling in builder internals.
// ============================================================================

#include <chronon3d/scene/model/render/resolved_types.hpp>
#include <vector>
#include <memory_resource>

namespace chronon3d {
    class Scene;
}

namespace chronon3d::graph {
    struct RenderGraphContext;
}

namespace chronon3d::graph::detail {

/// Result of resolving a scene's layer hierarchy into a flat list.
struct LayerResolutionResult {
    std::pmr::vector<chronon3d::ResolvedLayer> layers;
    chronon3d::ResolvedCamera camera;
};

/// Resolve a scene's layer hierarchy into a flat list of resolved layers
/// and the effective 2.5D camera.  Layers are ordered back-to-front and
/// parent transforms are composed.  Resolution is parallelised with TBB.
LayerResolutionResult resolve_layers(const Scene& scene,
                                     const RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
