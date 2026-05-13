#include <chronon3d/render_graph/graph_profiler.hpp>
#include <sstream>
#include <iomanip>

namespace chronon3d::graph {

std::string RenderProfiler::generate_report_json() const {
    std::ostringstream out;
    out << "{\n";
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
