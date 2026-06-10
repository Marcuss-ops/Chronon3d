#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <string>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_framebuffer_pool(std::stringstream& out, const ReportModel& model) {
    out << "## Framebuffer Pool (runtime snapshot)\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    for (const auto& pool : model.framebuffer_pool) {
        if (pool.name.find("_bytes") != std::string::npos) {
            out << "| " << pool.name << " | " << format_bytes(pool.value) << " |\n";
        } else {
            out << "| " << pool.name << " | " << pool.value << " |\n";
        }
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
