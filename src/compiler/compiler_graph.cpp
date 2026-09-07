#include "compiler_graph.hpp"

#include <chronon3d/compiler/detail/graph_compile_pipeline.hpp>

#include <utility>

namespace chronon3d::compiler::detail {

graph::CompiledFrameGraph compile_graph(
    graph::RenderGraph graph,
    graph::RenderGraphContext& context,
    const graph::FrameGraphCompileOptions& options) {
    graph::GraphCompilePipeline implementation;
    return implementation.compile(std::move(graph), context, options);
}

}  // namespace chronon3d::compiler::detail
