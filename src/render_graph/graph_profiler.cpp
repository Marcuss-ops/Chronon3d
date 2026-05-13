#include <chronon3d/render_graph/graph_profiler.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace chronon3d::graph {

ProfilerSummary RenderProfiler::get_summary() const {
    ProfilerSummary s;
    if (m_history.empty()) return s;

    double total_frame_time = 0;
    std::map<std::string, double> node_times;

    for (const auto& frame : m_history) {
        total_frame_time += frame.total_time_ms;
        for (const auto& node : frame.nodes) {
            s.total_nodes++;
            if (node.cache_hit) s.cache_hits++;
            else {
                s.cache_misses++;
                node_times[node.name] += node.execution_time_ms;
            }
        }
    }

    s.hit_rate = s.total_nodes > 0 ? static_cast<double>(s.cache_hits) / s.total_nodes : 0.0;
    s.avg_frame_time_ms = total_frame_time / m_history.size();

    // Find top 5 slowest nodes
    std::vector<std::pair<std::string, double>> sorted_nodes(node_times.begin(), node_times.end());
    std::sort(sorted_nodes.begin(), sorted_nodes.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    for (size_t i = 0; i < std::min<size_t>(5, sorted_nodes.size()); ++i) {
        s.slowest_nodes.push_back(sorted_nodes[i]);
    }

    return s;
}

std::string RenderProfiler::generate_report_json() const {
    auto summary = get_summary();
    std::ostringstream out;
    out << "{\n";
    out << "  \"summary\": {\n";
    out << "    \"total_nodes\": " << summary.total_nodes << ",\n";
    out << "    \"cache_hits\": " << summary.cache_hits << ",\n";
    out << "    \"cache_misses\": " << summary.cache_misses << ",\n";
    out << "    \"hit_rate\": " << std::fixed << std::setprecision(4) << summary.hit_rate << ",\n";
    out << "    \"avg_frame_time_ms\": " << summary.avg_frame_time_ms << ",\n";
    out << "    \"slowest_nodes\": [\n";
    for (size_t i = 0; i < summary.slowest_nodes.size(); ++i) {
        out << "      { \"name\": \"" << summary.slowest_nodes[i].first << "\", \"total_time_ms\": " << summary.slowest_nodes[i].second << " }"
            << (i + 1 < summary.slowest_nodes.size() ? "," : "") << "\n";
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"frames\": [\n";

    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& frame = m_history[i];
        out << "    {\n";
        out << "      \"frame\": " << frame.frame << ",\n";
        out << "      \"total_time_ms\": " << frame.total_time_ms << ",\n";
        out << "      \"nodes\": [\n";

        for (size_t j = 0; j < frame.nodes.size(); ++j) {
            const auto& node = frame.nodes[j];
            out << "        {\n";
            out << "          \"name\": \"" << node.name << "\",\n";
            out << "          \"kind\": " << static_cast<int>(node.kind) << ",\n";
            out << "          \"execution_time_ms\": " << node.execution_time_ms << ",\n";
            out << "          \"cache_hit\": " << (node.cache_hit ? "true" : "false") << ",\n";
            out << "          \"memory_bytes\": " << node.memory_bytes << "\n";
            out << "        }" << (j + 1 < frame.nodes.size() ? "," : "") << "\n";
        }

        out << "      ]\n";
        out << "    }" << (i + 1 < m_history.size() ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return out.str();
}

} // namespace chronon3d::graph
