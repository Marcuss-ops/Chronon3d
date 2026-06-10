#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_hot_work_attribution(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    (void)model;
    out << "## Hot Work Attribution\n";
    out << "| Work | Wall | Pixels | Est. Cores | CPU ms | GB/s |\n";
    out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";

    for (const auto& wa : analysis.work_attributions) {
        out << "| " << wa.work_name << " | " << format_ms(wa.wall_ms)
            << " | " << wa.pixels
            << " | " << fmt::format("{:.2f}", wa.est_cores)
            << " | " << format_ms(wa.cpu_ms)
            << " | ";
        if (wa.blocked_io) {
            out << "blocked";
        } else {
            out << fmt::format("{:.2f}", wa.gb_s);
        }
        out << " |\n";
    }

    if (model.bytes_avoided > 0) {
        out << "| ClearNode bytes avoided | — | — | — | — | "
            << format_bytes(model.bytes_avoided) << " |\n";
    }

    // Parse node_execute_actual_ms
    uint64_t node_exec_ms = 0;
    for (const auto& counter : model.phase_cpu_efficiency_counters) {
        if (counter.name == "node_execute_actual_ms") {
            node_exec_ms = counter.value;
            break;
        }
    }

    const uint64_t clr_memcpy_ms = analysis.work_attributions.size() > 0 ? static_cast<uint64_t>(analysis.work_attributions[0].wall_ms) : 0; // rough estimation check
    // Wait, let's just calculate total explained directly from analysis.work_attributions
    double total_attributed = 0.0;
    for (const auto& wa : analysis.work_attributions) {
        total_attributed += wa.wall_ms;
    }

    out << "\n**Attribution coverage:** " << format_ms(total_attributed)
        << " / " << format_ms(static_cast<double>(node_exec_ms))
        << " (" << fmt::format("{:.1f}%", analysis.attribution_coverage) << ")";
    if (analysis.attribution_low_coverage) {
        out << " ⚠️ Missing: executor overhead, small-node dispatch, idle wait";
    }
    out << "\n\n";
}

} // namespace chronon3d::cli::report_sections
#endif
