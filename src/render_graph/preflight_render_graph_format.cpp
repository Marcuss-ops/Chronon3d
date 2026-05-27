#include "preflight_render_graph_format.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace chronon3d::graph {

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

} // namespace chronon3d::graph
