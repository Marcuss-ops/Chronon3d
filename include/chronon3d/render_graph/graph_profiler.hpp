#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chrono>
#include <map>
#include <vector>
#include <string>

namespace chronon3d::graph {

struct NodeProfile {
    std::string name;
    RenderGraphNodeKind kind;
    double execution_time_ms{0.0};
    bool cache_hit{false};
    usize memory_bytes{0};
};

struct FrameProfile {
    Frame frame;
    std::vector<NodeProfile> nodes;
    double total_time_ms{0.0};
};

struct ProfilerSummary {
    usize total_nodes{0};
    usize cache_hits{0};
    usize cache_misses{0};
    double hit_rate{0.0};
    double avg_frame_time_ms{0.0};
    std::vector<std::pair<std::string, double>> slowest_nodes;
};

class RenderProfiler {
public:
    void begin_frame(Frame frame) {
        m_current_frame.frame = frame;
        m_current_frame.nodes.clear();
        m_frame_start = std::chrono::high_resolution_clock::now();
    }

    void end_frame() {
        auto end = std::chrono::high_resolution_clock::now();
        m_current_frame.total_time_ms = std::chrono::duration<double, std::milli>(end - m_frame_start).count();
        m_history.push_back(m_current_frame);
    }

    void record_node(const NodeProfile& profile) {
        m_current_frame.nodes.push_back(profile);
    }

    const std::vector<FrameProfile>& history() const { return m_history; }

    ProfilerSummary get_summary() const;
    std::string generate_report_json() const;

private:

    FrameProfile m_current_frame;
    std::vector<FrameProfile> m_history;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_frame_start;
};

} // namespace chronon3d::graph
