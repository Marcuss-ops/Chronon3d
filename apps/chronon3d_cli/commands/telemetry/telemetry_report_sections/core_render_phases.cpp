#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_core_render_phases(std::stringstream& out, const ReportModel& model) {
    out << "## Core Render Phases\n";
    out << "| Phase | Duration |\n";
    out << "| --- | --- |\n";
    for (const auto& phase : model.core_render_phases) {
        out << "| " << phase.phase_name << " | " << format_ms(phase.duration_ms) << " |\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
