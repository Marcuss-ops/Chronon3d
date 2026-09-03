#pragma once

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>

namespace chronon3d { struct RenderNode; }

namespace chronon3d::graph {

class FrameGraphCompiler {
public:
    [[nodiscard]] CompiledFrameGraph compile(
        RenderGraph graph,
        RenderGraphContext& ctx,
        const FrameGraphCompileOptions& options = {}
    ) const;

    // Compile a new graph against a prior compiled graph. When the topology
    // reuse predicate is safe, topology/processor metadata is reused while
    // the canonical compiled resource table is always rebuilt from the
    // current compiled DAG when lifetime planning is enabled.
    [[nodiscard]] CompiledFrameGraph compile_with_reuse(
        RenderGraph graph,
        RenderGraphContext& ctx,
        const CompiledFrameGraph& prior_compiled,
        const FrameGraphCompileOptions& options = {}
    ) const;

    [[nodiscard]] static std::uint64_t compute_structure_hash(
        const RenderGraph& graph,
        GraphNodeId output,
        std::uint64_t registry_generation = 0
    );

    static void validate_compiled_program_coverage(
        const CompiledFrameGraph& compiled);

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

    /// Build the sole persisted resource authority for the compiled graph.
    /// Lifetime analysis and physical allocation are intentionally one phase:
    /// ResourcePlanner is used as an ephemeral allocation engine and only the
    /// resulting canonical records/physical slots survive in the table.
    /// Concrete extent/format requirements are compiled from the same frame
    /// contract so downstream execution never reconstructs them independently.
    void build_compiled_resource_table(
        CompiledFrameGraph& compiled,
        const RenderGraphContext& ctx
    ) const;

    void validate_renderable_shape(
        const ::chronon3d::RenderNode& render_node,
        const CompiledNodeInfo& node_info,
        const RenderGraphContext& ctx
    ) const;

    void validate_renderable_graph(
        const RenderGraph& graph,
        GraphNodeId output,
        const RenderGraphContext& ctx
    ) const;

    void validate(
        const CompiledFrameGraph& compiled
    ) const;
};

} // namespace chronon3d::graph
