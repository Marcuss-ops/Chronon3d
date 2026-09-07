#include "compiler_graph.hpp"

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>

#include <utility>

namespace chronon3d::compiler::detail {

graph::CompiledFrameGraph compile_graph(
    graph::RenderGraph graph,
    graph::RenderGraphContext& context,
    const graph::FrameGraphCompileOptions& options) {
    graph::FrameGraphCompiler implementation;
    return implementation.compile(std::move(graph), context, options);
}

}  // namespace chronon3d::compiler::detail
