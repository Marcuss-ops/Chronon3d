// ---------------------------------------------------------------------------
// analysis_diagnostics.cpp
//
// Diagnostic/warning phases for debug_preflight_render_graph():
// topological warnings (dead nodes, redundant transforms), asset integrity,
// coordinate-space mismatch, dirty chain propagation.
//
// Extracted from analysis.cpp to keep each analysis concern in its own file.
// ---------------------------------------------------------------------------

#include "analysis.hpp"
#include "analysis_helpers.hpp"
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ──────────────────────────────────────────────────────────────────────
// Section 7: Topological warnings (dead nodes, redundant transforms)
// ──────────────────────────────────────────────────────────────────────

void check_topological_warnings(
    const RenderGraph& graph,
    GraphPreflightReport& report
) {
    std::vector<bool> reachable(graph.size(), false);
    if (graph.has_output()) {
        std::vector<GraphNodeId> stack{graph.output()};
        while (!stack.empty()) {
            GraphNodeId id = stack.back();
            stack.pop_back();
            if (id < graph.size() && !reachable[id] && graph.has_node(id)) {
                reachable[id] = true;
                for (GraphNodeId in : graph.inputs(id)) {
                    stack.push_back(in);
                }
            }
        }
    }
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        auto& rec = report.nodes[i];
        
        if (graph.has_output() && !reachable[i] && i != graph.output() && rec.kind != "removed") {
            std::string dead_warn = "DEAD_NODE: Node '" + rec.name + "' is present in the graph but cannot reach the output node. It will be evaluated but its result is discarded.";
            if (rec.warning.empty()) rec.warning = dead_warn;
            report.warnings.push_back(dead_warn);
        }

        const auto& node = graph.node(i);
        if (node.kind() == RenderGraphNodeKind::Transform) {
            const auto& inputs = graph.inputs(i);
            if (inputs.size() == 1 && graph.has_node(inputs[0])) {
                const auto& parent = graph.node(inputs[0]);
                if (parent.kind() == RenderGraphNodeKind::Transform) {
                    std::string red_warn = "REDUNDANT_TRANSFORM: TransformNode '" + std::string{rec.name} + "' has another TransformNode '" + std::string{parent.name()} + "' as its direct input. Consider fusing or eliminating redundant transforms.";
                    if (rec.warning.empty()) rec.warning = red_warn;
                    report.warnings.push_back(red_warn);
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Section 8: Asset integrity check
// ──────────────────────────────────────────────────────────────────────

void check_asset_integrity(
    const RenderGraph& graph,
    GraphPreflightReport& report
) {
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        auto& rec = report.nodes[i];
        const auto& node = graph.node(i);
        std::vector<std::string> asset_warnings;
        if (auto* src = dynamic_cast<const SourceNode*>(&node)) {
            check_shape_assets(src->render_node().shape, rec.name, asset_warnings);
        } else if (auto* msrc = dynamic_cast<const MultiSourceNode*>(&node)) {
            for (const auto& item : msrc->items()) {
                if (item.node) {
                    check_shape_assets(item.node->shape, rec.name, asset_warnings);
                }
            }
        } else if (auto* vid = dynamic_cast<const VideoNode*>(&node)) {
            const std::string path = vid->source().path;
            if (!path.empty()) {
                std::string resolved = resolve_asset_path(path);
                if (!std::filesystem::exists(resolved)) {
                    asset_warnings.push_back("MISSING_ASSET: Video file does not exist: \"" + path + "\"");
                }
            }
        }
        for (const auto& w : asset_warnings) {
            if (rec.warning.empty()) rec.warning = w;
            report.warnings.push_back(w);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Section 9: Coordinate space mismatch check
// ──────────────────────────────────────────────────────────────────────

void check_coordinate_mismatch(
    const RenderGraph& graph,
    const std::vector<GraphNodeId>& topo_order,
    GraphPreflightReport& report
) {
    std::vector<bool> node_is_3d(graph.size(), false);
    for (GraphNodeId u : topo_order) {
        const auto& node = graph.node(u);
        if (auto* src = dynamic_cast<const SourceNode*>(&node)) {
            if (src->uses_2_5d_projection()) node_is_3d[u] = true;
        } else if (auto* msrc = dynamic_cast<const MultiSourceNode*>(&node)) {
            if (msrc->uses_2_5d_projection()) node_is_3d[u] = true;
        } else {
            for (GraphNodeId v : graph.inputs(u)) {
                if (graph.has_node(v) && node_is_3d[v]) {
                    node_is_3d[u] = true;
                    break;
                }
            }
        }
    }
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        auto& rec = report.nodes[i];
        const auto& node = graph.node(i);
        if (node.kind() == RenderGraphNodeKind::Composite) {
            const auto& inputs = graph.inputs(i);
            if (inputs.size() >= 2) {
                bool has_3d_input = false;
                bool has_2d_input = false;
                for (GraphNodeId in : inputs) {
                    if (graph.has_node(in)) {
                        if (node_is_3d[in]) has_3d_input = true;
                        else has_2d_input = true;
                    }
                }
                if (has_3d_input && has_2d_input) {
                    std::string coord_warn = "COORDINATE_MISMATCH: CompositeNode '" + rec.name + "' blends 3D and 2D inputs together. This may cause alignment artifacts.";
                    if (rec.warning.empty()) rec.warning = coord_warn;
                    report.warnings.push_back(coord_warn);
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Section 10: Dirty chain propagation
// ──────────────────────────────────────────────────────────────────────

void propagate_dirty_chain(
    const RenderGraph& graph,
    const std::vector<GraphNodeId>& topo_order,
    GraphPreflightReport& report
) {
    for (GraphNodeId u : topo_order) {
        auto& rec = report.nodes[u];
        if (rec.frame_dependent) {
            rec.dirty = true;
            rec.dirty_reasons.push_back("Node is frame-dependent (animated parameters or time-varying input)");
        }
        for (GraphNodeId v : graph.inputs(u)) {
            if (!graph.has_node(v)) continue;
            const auto& parent_rec = report.nodes[v];
            if (parent_rec.dirty) {
                rec.dirty = true;
                std::string reason = "Input node '" + parent_rec.name + "' is dirty";
                if (!parent_rec.dirty_reasons.empty()) {
                    reason += " because: [" + parent_rec.dirty_reasons[0] + "]";
                }
                if (std::find(rec.dirty_reasons.begin(), rec.dirty_reasons.end(), reason) == rec.dirty_reasons.end()) {
                    rec.dirty_reasons.push_back(reason);
                }
            }
        }
        rec.cached = rec.cacheable && !rec.dirty;
    }
}

} // namespace chronon3d::graph
