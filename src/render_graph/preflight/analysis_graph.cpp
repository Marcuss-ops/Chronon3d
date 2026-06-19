// ---------------------------------------------------------------------------
// analysis_graph.cpp
//
// Graph structure analysis phases for debug_preflight_render_graph():
// node population (spatial + base properties), topological sort,
// peak memory simulation, aggregate metrics.
//
// Extracted from analysis.cpp to keep each analysis concern in its own file.
// ---------------------------------------------------------------------------

#include "analysis.hpp"
#include "analysis_helpers.hpp"
#include "format.hpp"
#include <chronon3d/render_graph/pipeline/graph_filter.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ──────────────────────────────────────────────────────────────────────
// Section 3: Populate initial node reports (spatial + base properties)
// ──────────────────────────────────────────────────────────────────────

void populate_node_basics(
    const RenderGraph& graph,
    const RenderGraphContext& ctx,
    const std::vector<int>& output_degree,
    const raster::BBox& canvas,
    int64_t canvas_area,
    GraphPreflightReport& report
) {
    report.nodes.reserve(graph.size());

    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        GraphPreflightNode rec;

        if (!graph.has_node(i)) {
            rec.name = "(removed:" + std::to_string(i) + ")";
            rec.kind = "removed";
            report.nodes.push_back(std::move(rec));
            continue;
        }

        const RenderGraphNode& node = graph.node(i);

        rec.name     = node.name();
        rec.kind     = std::string(to_string(node.kind()));
        rec.layer_id = node.layer_id();

        rec.cache_enabled   = node.cache_policy().enabled();
        rec.is_frame_variant = node.cache_policy().is_frame_variant();
        rec.dirty           = rec.is_frame_variant;
        rec.cached          = rec.cache_enabled && !rec.is_frame_variant;

        rec.input_count  = static_cast<int>(graph.inputs(i).size());
        rec.output_count = output_degree[i];

        auto bbox_opt = node.predicted_bbox(ctx, {});
        if (bbox_opt.has_value()) {
            rec.predicted_bbox = *bbox_opt;
        } else {
            rec.predicted_bbox = canvas;
        }

        rec.canvas_bbox      = canvas;
        rec.intersection_bbox = bbox_intersect(rec.predicted_bbox, canvas);

        const int64_t pred_area  = bbox_area(rec.predicted_bbox);
        const int64_t isect_area = bbox_area(rec.intersection_bbox);

        if (pred_area > 0) {
            rec.visible_ratio = static_cast<float>(isect_area) / static_cast<float>(pred_area);
        } else {
            rec.visible_ratio = (isect_area > 0) ? 1.0f : 0.0f;
        }

        if (isect_area == 0) {
            rec.outside_canvas  = true;
            rec.partially_clipped = false;
            rec.visibility      = VisibilityStatus::OutsideCanvas;
        } else if (bbox_opt.has_value() &&
                   rec.predicted_bbox.x0 <= 0 &&
                   rec.predicted_bbox.y0 <= 0 &&
                   rec.predicted_bbox.x1 >= ctx.frame.width &&
                   rec.predicted_bbox.y1 >= ctx.frame.height) {
            rec.outside_canvas    = false;
            rec.partially_clipped = false;
            rec.visibility        = VisibilityStatus::FullyVisible;
            rec.visible_ratio     = 1.0f;
        } else if (bbox_opt.has_value() && (
                rec.predicted_bbox.x0 < 0 ||
                rec.predicted_bbox.y0 < 0 ||
                rec.predicted_bbox.x1 > ctx.frame.width ||
                rec.predicted_bbox.y1 > ctx.frame.height
        )) {
            rec.outside_canvas    = false;
            rec.partially_clipped = true;
            rec.visibility        = VisibilityStatus::PartiallyClipped;
        } else {
            rec.outside_canvas    = false;
            rec.partially_clipped = false;
            rec.visibility        = VisibilityStatus::FullyVisible;
        }

        if (bbox_opt.has_value()) {
            if (rec.outside_canvas) {
                rec.warning = std::string(to_string(VisibilityStatus::OutsideCanvas)) +
                    ": node=\"" + rec.name + "\""
                    " layer=\"" + rec.layer_id + "\""
                    " global_bbox=[" +
                    std::to_string(rec.predicted_bbox.x0) + "," +
                    std::to_string(rec.predicted_bbox.y0) + "," +
                    std::to_string(rec.predicted_bbox.x1) + "," +
                    std::to_string(rec.predicted_bbox.y1) + "]";
                report.warnings.push_back(rec.warning);
            } else if (rec.partially_clipped) {
                rec.warning = std::string(to_string(VisibilityStatus::PartiallyClipped)) +
                    ": node=\"" + rec.name + "\""
                    " layer=\"" + rec.layer_id + "\""
                    " visible_ratio=" + std::to_string(rec.visible_ratio);
                report.warnings.push_back(rec.warning);
            }
        }

        // Estimate memory footprint
        size_t mem_bytes = 0;
        int64_t w = rec.predicted_bbox.x1 - rec.predicted_bbox.x0;
        int64_t h = rec.predicted_bbox.y1 - rec.predicted_bbox.y0;
        if (w > 0 && h > 0) {
            mem_bytes = static_cast<size_t>(w * h * 16);
        } else if (node.kind() == RenderGraphNodeKind::Output) {
            mem_bytes = static_cast<size_t>(ctx.frame.width * ctx.frame.height * 16);
        }
        rec.predicted_memory_bytes = mem_bytes;

        if (mem_bytes > 64 * 1024 * 1024) {
            std::string mem_warn = "HIGH_MEMORY_PRESSURE: Node '" + rec.name + "' has a predicted memory footprint of " + format_memory(mem_bytes) + " which exceeds the 64MB warning threshold.";
            if (rec.warning.empty()) rec.warning = mem_warn;
            report.warnings.push_back(mem_warn);
        }

        if (rec.visible_ratio < 0.15f && pred_area > canvas_area * 0.5f) {
            std::string clip_warn = "CLIPPING_BOTTLENECK: Node '" + rec.name + "' is heavily clipped (visible ratio: " + std::to_string(rec.visible_ratio) + ") but has a large off-screen predicted area (" + std::to_string(pred_area) + " pixels). Try optimizing its source bounds or transform.";
            if (rec.warning.empty()) rec.warning = clip_warn;
            report.warnings.push_back(clip_warn);
        }

        // Complexity score
        int base_complexity = 1;
        if (auto* src = dynamic_cast<const SourceNode*>(&node)) {
            base_complexity = get_shape_complexity(src->render_node().shape.type);
        } else if (auto* msrc = dynamic_cast<const MultiSourceNode*>(&node)) {
            base_complexity = 0;
            for (const auto& item : msrc->items()) {
                if (item.node) {
                    base_complexity += get_shape_complexity(item.node->shape.type);
                }
            }
            if (base_complexity == 0) base_complexity = 1;
        } else if (node.kind() == RenderGraphNodeKind::Transform) {
            base_complexity = 3;
        } else if (auto* eff = dynamic_cast<const EffectStackNode*>(&node)) {
            base_complexity = 5;
            for (const auto& inst : eff->effects()) {
                if (inst.enabled && inst.descriptor.id == "MotionBlur") {
                    base_complexity += 10;
                }
            }
        } else if (auto* adj = dynamic_cast<const AdjustmentNode*>(&node)) {
            base_complexity = 5;
            for (const auto& inst : adj->effects()) {
                if (inst.enabled && inst.descriptor.id == "MotionBlur") {
                    base_complexity += 10;
                }
            }
        } else if (node.kind() == RenderGraphNodeKind::Video) {
            base_complexity = 5;
        } else if (node.kind() == RenderGraphNodeKind::Mask) {
            base_complexity = 5;
        }

        float area_scale = static_cast<float>(pred_area) / (canvas_area > 0 ? canvas_area : 1.0f);
        rec.complexity_score = std::max(1, static_cast<int>(base_complexity * (1.0f + area_scale)));

        report.nodes.push_back(std::move(rec));
    }
}

// ──────────────────────────────────────────────────────────────────────
// Section 4–5: Topological sort + Peak memory simulation
// ──────────────────────────────────────────────────────────────────────

std::vector<GraphNodeId> topo_sort_and_peak_memory(
    const RenderGraph& graph,
    const std::vector<int>& output_degree,
    GraphPreflightReport& report
) {
    std::vector<GraphNodeId> topo_order;
    {
        std::vector<bool> visited_topo(graph.size(), false);
        std::vector<bool> in_progress(graph.size(), false);
        auto dfs = [&](auto& self, GraphNodeId u) -> void {
            visited_topo[u] = true;
            in_progress[u] = true;
            for (GraphNodeId v : graph.inputs(u)) {
                if (!graph.has_node(v)) continue;
                if (!visited_topo[v]) {
                    self(self, v);
                }
            }
            in_progress[u] = false;
            topo_order.push_back(u);
        };
        for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
            if (graph.has_node(i) && !visited_topo[i]) {
                dfs(dfs, i);
            }
        }
    }

    size_t current_mem = 0;
    size_t peak_mem = 0;
    std::vector<int> remaining_consumers(graph.size(), 0);
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (graph.has_node(i)) {
            remaining_consumers[i] = output_degree[i];
        }
    }
    for (GraphNodeId u : topo_order) {
        const auto& rec = report.nodes[u];
        if (rec.predicted_memory_bytes > 0) {
            current_mem += rec.predicted_memory_bytes;
            peak_mem = std::max(peak_mem, current_mem);
        }
        for (GraphNodeId v : graph.inputs(u)) {
            if (!graph.has_node(v)) continue;
            remaining_consumers[v]--;
            if (remaining_consumers[v] == 0 && !report.nodes[v].cached) {
                current_mem -= report.nodes[v].predicted_memory_bytes;
            }
        }
    }
    report.peak_memory_bytes = peak_mem;

    return topo_order;
}

// ──────────────────────────────────────────────────────────────────────
// Section 6: Aggregate metrics
// ──────────────────────────────────────────────────────────────────────

void compute_aggregate_metrics(
    const RenderGraph& graph,
    int64_t canvas_area,
    GraphPreflightReport& report
) {
    int total_live_nodes = 0;
    int cached_nodes = 0;
    size_t total_fill_rate = 0;
    int fullscreen_count = 0;

    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        const auto& rec = report.nodes[i];
        total_live_nodes++;
        if (rec.cached) {
            cached_nodes++;
        }
        report.total_complexity_score += rec.complexity_score;
        total_fill_rate += bbox_area(rec.predicted_bbox);

        if (bbox_area(rec.predicted_bbox) >= 0.95f * canvas_area) {
            fullscreen_count++;
        }
    }
    report.cache_score = total_live_nodes > 0 ? (cached_nodes * 100) / total_live_nodes : 0;
    report.total_fill_rate_pixels = total_fill_rate;

    if (fullscreen_count > 4) {
        std::string overdraw_warn = "EXCESSIVE_OVERDRAW: Found " + std::to_string(fullscreen_count) + " full-screen nodes overlapping. Consider baking static background layers to avoid excessive fill-rate overhead.";
        report.warnings.push_back(overdraw_warn);
    }
}

} // namespace chronon3d::graph
