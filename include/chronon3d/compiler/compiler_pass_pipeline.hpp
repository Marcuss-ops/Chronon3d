#pragma once

#include <chronon3d/render_graph/builder/graph_build_pass.hpp>
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include <memory>
#include <vector>

namespace chronon3d::compiler {

/// Compiler-owned lowering pipeline. Graph construction is a compiler pass
/// sequence, not a second render-graph authority.
class CompilerPassPipeline final {
public:
    CompilerPassPipeline() = default;

    void add_pass(std::unique_ptr<graph::GraphBuildPass> pass);
    void add_default_passes();

    [[nodiscard]] graph::RenderGraph build(
        const Scene& scene, graph::RenderGraphContext& context);

    [[nodiscard]] graph::RenderGraph build_with_resolved(
        const Scene& scene,
        graph::RenderGraphContext& context,
        const graph::GraphBuildContext::ResolvedData& resolved);

    [[nodiscard]] std::size_t pass_count() const { return passes_.size(); }

private:
    [[nodiscard]] graph::RenderGraph run_passes(
        graph::GraphBuildContext& context);

    std::vector<std::unique_ptr<graph::GraphBuildPass>> passes_;
};

}  // namespace chronon3d::compiler
