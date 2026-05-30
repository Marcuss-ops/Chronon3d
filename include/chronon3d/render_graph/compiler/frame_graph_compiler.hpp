#pragma once

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>

namespace chronon3d::graph {

class FrameGraphCompiler {
public:
    [[nodiscard]] CompiledFrameGraph compile(
        RenderGraph graph,
        RenderGraphContext& ctx,
        const FrameGraphCompileOptions& options = {}
    ) const;

    [[nodiscard]] static std::uint64_t compute_structure_hash(
        const RenderGraph& graph,
        GraphNodeId output
    );

private:
    void build_execution_levels(
        RenderGraph& graph,
        GraphNodeId output,
        CompiledFrameGraph& compiled
    ) const;

    void build_node_metadata(
        RenderGraph& graph,
        RenderGraphContext& ctx,
        CompiledFrameGraph& compiled,
        const FrameGraphCompileOptions& options
    ) const;

    void compute_resource_lifetimes(
        CompiledFrameGraph& compiled
    ) const;

    void validate(
        const CompiledFrameGraph& compiled
    ) const;
};

} // namespace chronon3d::graph
