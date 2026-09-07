#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>

namespace chronon3d::compiler {

/// Compiler-root entrypoint for runtime graph lowering.
[[nodiscard]] graph::CompiledFrameGraph compile_graph(
    graph::RenderGraph graph,
    graph::RenderGraphContext& context,
    const graph::FrameGraphCompileOptions& options = {});

}  // namespace chronon3d::compiler
