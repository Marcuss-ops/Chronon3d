#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>

namespace chronon3d::compiler::detail {

/// Internal graph-lowering seam owned by the compiler object library.
///
/// This is deliberately not part of the public compiler API: graph building
/// and frame-graph lowering are compiler implementation details.  Production
/// consumers must enter through the compiler/job boundary instead of keeping
/// a second graph-compiler facade alive.
[[nodiscard]] graph::CompiledFrameGraph compile_graph(
    graph::RenderGraph graph,
    graph::RenderGraphContext& context,
    const graph::FrameGraphCompileOptions& options = {});

}  // namespace chronon3d::compiler::detail
