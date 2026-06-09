#pragma once

#include "graph_build_pass.hpp"
#include "graph_build_context.hpp"

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <memory>
#include <vector>

namespace chronon3d {
    class Scene;
}

namespace chronon3d::graph {

/// Orchestrates a sequence of `GraphBuildPass` instances to build a
/// `RenderGraph` from a `Scene`.
///
/// Usage:
/// ```cpp
/// GraphBuildPipeline pipeline;
/// pipeline.add_default_passes();
/// RenderGraph graph = pipeline.build(scene, ctx);
/// ```
class GraphBuildPipeline {
public:
    GraphBuildPipeline() = default;

    /// Register a pass.  Passes run in the order they are added.
    void add_pass(std::unique_ptr<GraphBuildPass> pass);

    /// Register the built-in default passes.
    void add_default_passes();

    /// Build a render graph by running all registered passes.
    [[nodiscard]] RenderGraph build(const Scene& scene, RenderGraphContext& ctx);

    /// Build a render graph using pre-resolved layer data.
    /// This avoids a redundant `resolve_layers()` call when the caller
    /// has already resolved the scene (e.g. for dirty-rect computation).
    [[nodiscard]] RenderGraph build_with_resolved(const Scene& scene,
                                                   RenderGraphContext& ctx,
                                                   const GraphBuildContext::ResolvedData& resolved);

    /// Number of registered passes (for diagnostics).
    [[nodiscard]] std::size_t pass_count() const { return m_passes.size(); }

private:
    RenderGraph run_passes(GraphBuildContext& build_ctx);

    std::vector<std::unique_ptr<GraphBuildPass>> m_passes;
};

} // namespace chronon3d::graph
