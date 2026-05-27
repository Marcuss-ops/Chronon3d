// ---------------------------------------------------------------------------
// preflight_render_graph.cpp
//
// Implementation of debug_preflight_render_graph().
//
// Strategy:
//   1. Build the render graph exactly as debug_scene_graph() does (no execute).
//   2. Walk every node: query predicted_bbox(), cacheable(), frame_dependent().
//   3. Compute canvas intersection, visible_ratio, assign VisibilityStatus.
//   4. Emit warnings for outside / partially-clipped nodes.
//   5. Produce an enriched DOT string (bbox + layer_id + dirty in node labels).
//   6. Fill and return the GraphPreflightReport.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/preflight_render_graph.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "builder/graph_builder_pipeline.hpp"
#include "builder/graph_builder_internal.hpp"
#include "render_pipeline_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace chronon3d::graph {

// ── Helpers ────────────────────────────────────────────────────────────────

/// Compute the area of a BBox (0 if degenerate).
static int64_t bbox_area(const raster::BBox& b) {
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return 0;
    return static_cast<int64_t>(b.x1 - b.x0) * static_cast<int64_t>(b.y1 - b.y0);
}

/// Intersect two BBoxes; returns {0,0,0,0} if disjoint.
static raster::BBox bbox_intersect(const raster::BBox& a, const raster::BBox& b) {
    raster::BBox r;
    r.x0 = std::max(a.x0, b.x0);
    r.y0 = std::max(a.y0, b.y0);
    r.x1 = std::min(a.x1, b.x1);
    r.y1 = std::min(a.y1, b.y1);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) {
        return raster::BBox{0, 0, 0, 0};
    }
    return r;
}

/// Count how many nodes have `node_id` in their inputs list.
static int count_output_edges(const RenderGraph& graph, GraphNodeId node_id) {
    int count = 0;
    for (GraphNodeId consumer = 0;
         consumer < static_cast<GraphNodeId>(graph.size());
         ++consumer) {
        if (!graph.has_node(consumer)) continue;
        for (GraphNodeId inp : graph.inputs(consumer)) {
            if (inp == node_id) ++count;
        }
    }
    return count;
}

/// Build the enriched DOT string: node labels include layer_id, bbox, dirty state.
static std::string build_enriched_dot(
    const RenderGraph&              graph,
    const RenderGraphContext&       ctx,
    const std::vector<GraphPreflightNode>& node_reports
) {
    std::ostringstream out;
    out << "digraph RenderGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box, style=filled, fontname=\"Arial\", fontsize=10];\n";

    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        const RenderGraphNode& node = graph.node(i);

        std::string color = "white";
        switch (node.kind()) {
            case RenderGraphNodeKind::Source:    color = "#AED6F1"; break; // light blue
            case RenderGraphNodeKind::Transform: color = "#D5D8DC"; break; // light grey
            case RenderGraphNodeKind::Effect:    color = "#FCF3CF"; break; // light yellow
            case RenderGraphNodeKind::Composite: color = "#ABEBC6"; break; // light green
            case RenderGraphNodeKind::Output:    color = "#FAD7A0"; break; // orange
            case RenderGraphNodeKind::Video:     color = "#D7BDE2"; break; // purple
            case RenderGraphNodeKind::Mask:      color = "#FADBD8"; break; // pink
            default: break;
        }

        const auto& rep = node_reports[i];

        // Build label lines
        std::ostringstream label;
        label << node.name();
        if (!rep.layer_id.empty()) {
            label << "\\nlayer=" << rep.layer_id;
        }
        label << "\\nkind=" << rep.kind;

        // bbox
        const auto& bbox = rep.predicted_bbox;
        label << "\\nbbox=[" << bbox.x0 << "," << bbox.y0
              << "," << bbox.x1 << "," << bbox.y1 << "]";

        // visibility
        label << "\\nvisible_ratio=" << std::fixed << std::setprecision(2) << rep.visible_ratio;

        // dirty / cached
        label << "\\ndirty=" << (rep.dirty ? "Y" : "N")
              << " cached=" << (rep.cached ? "Y" : "N");

        if (!rep.warning.empty()) {
            label << "\\n⚠ " << rep.warning;
            color = "#F1948A"; // red tint for warnings
        }

        out << "  n" << i
            << " [label=\"" << label.str() << "\""
            << ", fillcolor=\"" << color << "\""
            << "];\n";
    }

    // Edges
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        for (GraphNodeId input : graph.inputs(i)) {
            out << "  n" << input << " -> n" << i << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}

// ── GraphPreflightReport methods ───────────────────────────────────────────

const GraphPreflightNode* GraphPreflightReport::find_node(std::string_view name) const {
    for (const auto& n : nodes) {
        if (n.name.find(name) != std::string::npos) return &n;
    }
    return nullptr;
}

bool GraphPreflightReport::has_warning_containing(std::string_view text) const {
    for (const auto& w : warnings) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}

std::string GraphPreflightReport::to_text() const {
    std::ostringstream out;

    out << "===== Graph Preflight Report =====\n";
    out << "Nodes: " << nodes.size() << "\n";
    out << "Warnings: " << warnings.size() << "\n\n";

    // Table header
    out << std::left
        << std::setw(30) << "Node"
        << std::setw(14) << "Kind"
        << std::setw(28) << "predicted_bbox"
        << std::setw(28) << "intersection"
        << std::setw(12) << "visible_ratio"
        << std::setw(18) << "visibility"
        << std::setw(8)  << "dirty"
        << std::setw(8)  << "cached"
        << "\n";
    out << std::string(148, '-') << "\n";

    for (const auto& n : nodes) {
        auto fmt_bbox = [](const raster::BBox& b) -> std::string {
            std::ostringstream s;
            s << "[" << b.x0 << "," << b.y0 << "," << b.x1 << "," << b.y1 << "]";
            return s.str();
        };

        out << std::left
            << std::setw(30) << (n.name.size() > 28 ? n.name.substr(0, 27) + "…" : n.name)
            << std::setw(14) << n.kind
            << std::setw(28) << fmt_bbox(n.predicted_bbox)
            << std::setw(28) << fmt_bbox(n.intersection_bbox)
            << std::setw(12) << std::fixed << std::setprecision(2) << n.visible_ratio
            << std::setw(18) << to_string(n.visibility)
            << std::setw(8)  << (n.dirty  ? "YES" : "no")
            << std::setw(8)  << (n.cached ? "YES" : "no")
            << "\n";

        if (!n.warning.empty()) {
            out << "  >>> " << n.warning << "\n";
        }
    }

    if (!warnings.empty()) {
        out << "\n--- WARNINGS ---\n";
        for (const auto& w : warnings) {
            out << "  [WARN] " << w << "\n";
        }
    } else {
        out << "\n--- No warnings. Graph looks clean. ---\n";
    }

    return out.str();
}

// ── Main implementation ────────────────────────────────────────────────────

GraphPreflightReport debug_preflight_render_graph(
    RenderBackend&             backend,
    cache::NodeCache&          node_cache,
    const Scene&               scene,
    const Camera&              camera,
    i32 width,
    i32 height,
    Frame  frame,
    f32    frame_time,
    const RenderSettings&      settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder*  video_decoder,
    float fps
) {
    // ── 1. Build context (same as debug_scene_graph) ─────────────────────
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height,
        frame, frame_time, settings, registry, video_decoder, fps
    );
    ctx.diagnostics_enabled = true;
    ctx.light_context = scene.light_context();
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d      = scene.camera_2_5d();
        ctx.has_camera_2_5d  = true;
        ctx.projection_ctx   = renderer::make_projection_context(ctx.camera_2_5d, width, height);
        ctx.projection_ctx.ready = true;
    }

    // ── 2. Build graph (no execution) ────────────────────────────────────
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    const raster::BBox canvas{0, 0, width, height};
    const int64_t canvas_area = static_cast<int64_t>(width) * static_cast<int64_t>(height);

    // ── 3. Walk every node and build per-node report ──────────────────────
    // Count output edges up front (cheap, avoids a second pass later)
    std::vector<int> output_degree(graph.size(), 0);
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        for (GraphNodeId inp : graph.inputs(i)) {
            if (inp < static_cast<GraphNodeId>(output_degree.size())) {
                ++output_degree[inp];
            }
        }
    }

    GraphPreflightReport report;
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

        // Cache / dirty flags
        rec.cacheable       = node.cacheable();
        rec.frame_dependent = node.frame_dependent();
        rec.dirty           = node.frame_dependent();           // dirty ↔ will be re-evaluated
        rec.cached          = node.cacheable() && !node.frame_dependent();

        // Topology
        rec.input_count  = static_cast<int>(graph.inputs(i).size());
        rec.output_count = output_degree[i];

        // Predicted bbox
        auto bbox_opt = node.predicted_bbox(ctx);
        if (bbox_opt.has_value()) {
            rec.predicted_bbox = *bbox_opt;
        } else {
            // Node didn't declare a bbox — assume it covers the full canvas
            // (conservative; cannot produce false positive visibility claim)
            rec.predicted_bbox = canvas;
        }

        rec.canvas_bbox      = canvas;
        rec.intersection_bbox = bbox_intersect(rec.predicted_bbox, canvas);

        // Visible ratio
        const int64_t pred_area  = bbox_area(rec.predicted_bbox);
        const int64_t isect_area = bbox_area(rec.intersection_bbox);

        if (pred_area > 0) {
            rec.visible_ratio = static_cast<float>(isect_area) / static_cast<float>(pred_area);
        } else {
            // Degenerate predicted_bbox or full-canvas fallback
            rec.visible_ratio = (isect_area > 0) ? 1.0f : 0.0f;
        }

        // Visibility status
        if (isect_area == 0) {
            rec.outside_canvas  = true;
            rec.partially_clipped = false;
            rec.visibility      = VisibilityStatus::OutsideCanvas;
        } else if (bbox_opt.has_value() && (
                rec.predicted_bbox.x0 < 0 ||
                rec.predicted_bbox.y0 < 0 ||
                rec.predicted_bbox.x1 > width ||
                rec.predicted_bbox.y1 > height
        )) {
            rec.outside_canvas    = false;
            rec.partially_clipped = true;
            rec.visibility        = VisibilityStatus::PartiallyClipped;
        } else {
            rec.outside_canvas    = false;
            rec.partially_clipped = false;
            rec.visibility        = VisibilityStatus::FullyVisible;
        }

        // Produce warning for nodes with real bbox info (not the canvas fallback)
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

        report.nodes.push_back(std::move(rec));
    }

    // ── 4. Build enriched DOT ─────────────────────────────────────────────
    report.dot = build_enriched_dot(graph, ctx, report.nodes);

    return report;
}

} // namespace chronon3d::graph
