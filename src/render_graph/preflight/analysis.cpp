// preflight_render_graph_analysis.cpp
// Analysis phases for debug_preflight_render_graph():
// node population, topological sort, peak memory simulation, aggregate metrics,
// topological warnings, asset integrity, coordinate-space mismatch, dirty chains.

#include "analysis.hpp"
#include "format.hpp"
#include <chronon3d/render_graph/graph_filter.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace chronon3d::graph {

namespace {

// ── Bounding box helpers ────────────────────────────────────────────

int64_t bbox_area(const raster::BBox& b) {
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return 0;
    return static_cast<int64_t>(b.x1 - b.x0) * static_cast<int64_t>(b.y1 - b.y0);
}

raster::BBox bbox_intersect(const raster::BBox& a, const raster::BBox& b) {
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

// ── Shape complexity ────────────────────────────────────────────────

int get_shape_complexity(ShapeType type) {
    switch (type) {
        case ShapeType::Rect:
        case ShapeType::RoundedRect:
        case ShapeType::Image:
            return 1;
        case ShapeType::GridBackground:
        case ShapeType::Circle:
        case ShapeType::Line:
            return 2;
        case ShapeType::Text:
            return 4;
        case ShapeType::Path:
            return 8;
        case ShapeType::Mesh:
        case ShapeType::FakeBox3D:
        case ShapeType::GridPlane:
        case ShapeType::FakeExtrudedText:
            return 6;
        default:
            return 1;
    }
}

void check_shape_assets(const Shape& shape, const std::string& node_name, std::vector<std::string>& warnings) {
    if (shape.type == ShapeType::Image) {
        const std::string path = shape.image.path;
        if (!path.empty()) {
            std::string resolved = AssetRegistry::resolve(path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: Image file does not exist: \"" + path + "\"");
            }
        }
    } else if (shape.type == ShapeType::Text) {
        const std::string font_path = shape.text.style.font_path;
        if (!font_path.empty()) {
            std::string resolved = AssetRegistry::resolve(font_path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: Font file does not exist: \"" + font_path + "\"");
            }
        }
    } else if (shape.type == ShapeType::FakeExtrudedText) {
        const std::string font_path = shape.fake_extruded_text.font_path;
        if (!font_path.empty()) {
            std::string resolved = AssetRegistry::resolve(font_path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: Font file does not exist: \"" + font_path + "\"");
            }
        }
    }
}

} // anonymous namespace

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

        rec.cacheable       = node.cacheable();
        rec.frame_dependent = node.frame_dependent();
        rec.dirty           = node.frame_dependent();
        rec.cached          = node.cacheable() && !node.frame_dependent();

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
                   rec.predicted_bbox.x1 >= ctx.width &&
                   rec.predicted_bbox.y1 >= ctx.height) {
            rec.outside_canvas    = false;
            rec.partially_clipped = false;
            rec.visibility        = VisibilityStatus::FullyVisible;
            rec.visible_ratio     = 1.0f;
        } else if (bbox_opt.has_value() && (
                rec.predicted_bbox.x0 < 0 ||
                rec.predicted_bbox.y0 < 0 ||
                rec.predicted_bbox.x1 > ctx.width ||
                rec.predicted_bbox.y1 > ctx.height
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
            mem_bytes = static_cast<size_t>(ctx.width * ctx.height * 16);
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
                    std::string red_warn = "REDUNDANT_TRANSFORM: TransformNode '" + rec.name + "' has another TransformNode '" + parent.name() + "' as its direct input. Consider fusing or eliminating redundant transforms.";
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
                std::string resolved = AssetRegistry::resolve(path);
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

std::string format_memory(size_t bytes) {
    if (bytes >= 1024 * 1024) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
        return s.str();
    }
    if (bytes >= 1024) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / 1024.0) << " KB";
        return s.str();
    }
    return std::to_string(bytes) + " B";
}

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

std::string build_enriched_dot(
    const RenderGraph& graph,
    const RenderGraphContext&,
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
            case RenderGraphNodeKind::Source:    color = "#AED6F1"; break;
            case RenderGraphNodeKind::Transform: color = "#D5D8DC"; break;
            case RenderGraphNodeKind::Effect:    color = "#FCF3CF"; break;
            case RenderGraphNodeKind::Composite: color = "#ABEBC6"; break;
            case RenderGraphNodeKind::Output:    color = "#FAD7A0"; break;
            case RenderGraphNodeKind::Video:     color = "#D7BDE2"; break;
            case RenderGraphNodeKind::Mask:      color = "#FADBD8"; break;
            default: break;
        }

        const auto& rep = node_reports[i];

        std::ostringstream label;
        label << node.name();
        if (!rep.layer_id.empty()) {
            label << "\\nlayer=" << rep.layer_id;
        }
        label << "\\nkind=" << rep.kind;
        const auto& bbox = rep.predicted_bbox;
        label << "\\nbbox=[" << bbox.x0 << "," << bbox.y0
              << "," << bbox.x1 << "," << bbox.y1 << "]";
        label << "\\nvisible_ratio=" << std::fixed << std::setprecision(2) << rep.visible_ratio;
        label << "\\ndirty=" << (rep.dirty ? "Y" : "N")
              << " cached=" << (rep.cached ? "Y" : "N");

        if (!rep.warning.empty()) {
            label << "\\n⚠ " << rep.warning;
            color = "#F1948A";
        }

        out << "  n" << i
            << " [label=\"" << label.str() << "\""
            << ", fillcolor=\"" << color << "\""
            << "];\n";
    }

    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        for (GraphNodeId input : graph.inputs(i)) {
            out << "  n" << input << " -> n" << i << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}

std::string GraphPreflightReport::to_text() const {
    std::ostringstream out;

    out << "===== Graph Preflight Report =====\n";
    out << "Nodes: " << nodes.size() << "\n";
    out << "Warnings: " << warnings.size() << "\n";
    out << "Peak Memory: " << format_memory(peak_memory_bytes) << "\n";
    out << "Total Complexity: " << total_complexity_score << "\n";
    out << "Cache Score: " << cache_score << "%\n";
    out << "Total Fill Rate: " << total_fill_rate_pixels << " pixels\n\n";

    out << std::left
        << std::setw(24) << "Node"
        << std::setw(12) << "Kind"
        << std::setw(24) << "predicted_bbox"
        << std::setw(24) << "intersection"
        << std::setw(10) << "vis_ratio"
        << std::setw(16) << "visibility"
        << std::setw(6)  << "dirty"
        << std::setw(6)  << "cached"
        << std::setw(14) << "Memory"
        << std::setw(12) << "Complexity"
        << "\n";
    out << std::string(150, '-') << "\n";

    for (const auto& n : nodes) {
        auto fmt_bbox = [](const raster::BBox& b) -> std::string {
            std::ostringstream s;
            s << "[" << b.x0 << "," << b.y0 << "," << b.x1 << "," << b.y1 << "]";
            return s.str();
        };

        out << std::left
            << std::setw(24) << (n.name.size() > 22 ? n.name.substr(0, 21) + "…" : n.name)
            << std::setw(12) << n.kind
            << std::setw(24) << fmt_bbox(n.predicted_bbox)
            << std::setw(24) << fmt_bbox(n.intersection_bbox)
            << std::setw(10) << std::fixed << std::setprecision(2) << n.visible_ratio
            << std::setw(16) << to_string(n.visibility)
            << std::setw(6)  << (n.dirty  ? "YES" : "no")
            << std::setw(6)  << (n.cached ? "YES" : "no")
            << std::setw(14) << format_memory(n.predicted_memory_bytes)
            << std::setw(12) << n.complexity_score
            << "\n";

        if (!n.warning.empty()) {
            out << "  >>> " << n.warning << "\n";
        }
        if (!n.dirty_reasons.empty()) {
            for (const auto& dr : n.dirty_reasons) {
                out << "    * Dirty: " << dr << "\n";
            }
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

LayerGraphSelection find_layer_nodes(
    const RenderGraph& graph,
    const std::string& layer_id)
{
    LayerGraphSelection selection;

    for (GraphNodeId id = 0; id < static_cast<GraphNodeId>(graph.size()); ++id) {
        const auto& node = graph.node(id);
        if (node.layer_id() == layer_id) {
            selection.matching_nodes.push_back(id);
            selection.selected_output = id;
        }
    }

    return selection;
}

} // namespace chronon3d::graph
