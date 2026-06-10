#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_phase_cpu_efficiency(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    (void)model;
    out << "## Phase CPU Efficiency\n";
    out << "| Phase | Wall | Pixels | Est. Cores | CPU ms | GB/s |\n";
    out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";

    for (const auto& pe : analysis.phase_efficiencies) {
        out << "| " << pe.phase_name << " | " << format_ms(pe.wall_ms)
            << " | " << pe.pixels
            << " | " << fmt::format("{:.2f}", pe.est_cores)
            << " | " << format_ms(pe.cpu_ms)
            << " | " << fmt::format("{:.2f}", pe.gb_s)
            << " |\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
