// =============================================================================
// render_graph_context_clone.inc — Context cloning and asset-path resolution.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>

namespace chronon3d::graph {

RenderGraphContext RenderGraphContext::clone_for_node_execution() const {
    RenderGraphContext copy;
    copy.frame_input = frame_input;
    copy.policy      = policy;
    copy.services    = services;
    copy.node_exec.counters         = node_exec.counters;
    copy.node_exec.clip_rect        = node_exec.clip_rect;
    copy.node_exec.active_tile_clip = node_exec.active_tile_clip;
    copy.node_exec.dirty_rect       = node_exec.dirty_rect;
    copy.node_exec.shared_dof_depth = node_exec.shared_dof_depth
        ? node_exec.shared_dof_depth
        : const_cast<std::vector<float>*>(&node_exec.dof_depth);
    copy.node_exec.shared_dof_source_coverage =
        node_exec.shared_dof_source_coverage
            ? node_exec.shared_dof_source_coverage
            : const_cast<DofSourceCoverage*>(&node_exec.dof_source_coverage);
    copy.node_exec.current_identity = node_exec.current_identity;
    copy.node_exec.planned_physical_slot = node_exec.planned_physical_slot;
    copy.node_exec.current_shape_processor = node_exec.current_shape_processor;
    copy.node_exec.current_shape_processors = node_exec.current_shape_processors;
    copy.node_exec.current_effect_processors = node_exec.current_effect_processors;
    copy.node_exec.processor_snapshot = node_exec.processor_snapshot;
    copy.node_exec.processor_bindings_compiled = node_exec.processor_bindings_compiled;
    // Per-node scratch views intentionally remain default-initialized.
    copy.frame_error = frame_error;
    return copy;
}

std::string RenderGraphContext::resolve_asset(const std::string& relative_path) const {
    if (relative_path.empty()) return relative_path;
    if (!relative_path.empty() && relative_path[0] == '/') return relative_path;
    const auto& root = frame_input.assets_root;
    if (root.empty()) return relative_path;
    if (!root.empty() && root.back() == '/') {
        return root + relative_path;
    }
    return root + "/" + relative_path;
}

} // namespace chronon3d::graph
