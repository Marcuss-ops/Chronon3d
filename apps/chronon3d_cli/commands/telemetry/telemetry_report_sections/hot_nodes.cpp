#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_hot_nodes(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    out << "## Hot Nodes\n";
    out << "| Node | Type | Calls | Total | Avg | Cache Hit Rate | Pixels Touched |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: |\n";

    for (const auto& node : model.hot_nodes) {
        const double cache_rate = node.cache_ops > 0
            ? static_cast<double>(node.cache_hits) / static_cast<double>(node.cache_ops)
            : 0.0;
        out << "| " << node.node_name << " | "
            << node.node_type << " | "
            << node.calls << " | "
            << format_ms(node.duration_ms) << " | "
            << format_ms(node.avg_duration_ms) << " | "
            << format_pct(cache_rate) << " | "
            << node.pixels_touched << " |\n";
    }

    uint64_t phase_costs_total_ms = 0;
    for (const auto& counter : model.phase_cost_counters) {
        if (counter.value == 0) continue;
        
        // Determine if this counter stores microseconds (suffix _us) vs milliseconds (_ms).
        const bool is_us = counter.name.size() > 3 &&
            counter.name.substr(counter.name.size() - 3) == "_us";
        const double display_value_ms = is_us
            ? static_cast<double>(counter.value) / 1000.0
            : static_cast<double>(counter.value);
        
        std::string label = counter.name;
        if (label == "clearnode_memcpy_ms") label = "ClearNode memcpy";
        else if (label == "clearnode_clear_ms") label = "ClearNode clear";
        else if (label == "clearnode_restore_ms") label = "ClearNode restore";
        else if (label == "framebuffer_copy_ms") label = "Framebuffer copy";
        else if (label == "compositenode_blend_ms") label = "Composite blend";
        else if (label == "compositenode_copy_ms") label = "Composite copy";
        else if (label == "compositenode_setup_ms") label = "Composite setup";
        else if (label == "compositenode_dispatch_ms") label = "Composite dispatch";
        else if (label == "compositenode_acquire_ms") label = "Composite acquire";
        else if (label == "compositenode_overhead_ms") label = "Composite overhead";
        else if (label == "compositenode_internal_us") label = "Composite internal (non-blend)";
        else if (label == "frame_conversion_copy_ms") label = "Frame conversion copy";
        else if (label == "video_pipe_write_ms") label = "Video pipe write";

        out << "| " << label << " | Overhead | 1 | "
            << format_ms(display_value_ms) << " | "
            << format_ms(display_value_ms) << " | — | 0 |\n";
        phase_costs_total_ms += static_cast<uint64_t>(display_value_ms);
    }

    // Parse node_execute_actual_ms
    uint64_t node_exec_ms = 0;
    for (const auto& counter : model.phase_cpu_efficiency_counters) {
        if (counter.name == "node_execute_actual_ms") {
            node_exec_ms = counter.value;
            break;
        }
    }

    out << "\n**Hot Nodes Coverage:** " << format_ms(static_cast<double>(model.hot_nodes_total_ms))
        << " / " << format_ms(static_cast<double>(node_exec_ms))
        << " (" << fmt::format("{:.1f}%", analysis.hot_nodes_coverage) << ")";
    if (phase_costs_total_ms > 0) {
        out << " — phase breakdown adds " << format_ms(static_cast<double>(phase_costs_total_ms)) << " in sub-costs";
    }
    if (analysis.hot_nodes_low_coverage) {
        out << " ⚠️ Low coverage — missing: executor overhead, pipeline wait, etc.";
    }
    out << "\n\n";
}

} // namespace chronon3d::cli::report_sections
#endif
