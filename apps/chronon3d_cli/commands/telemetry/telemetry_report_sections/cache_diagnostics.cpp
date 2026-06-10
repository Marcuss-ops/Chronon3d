#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_cache_diagnostics(std::stringstream& out, const ReportModel& model) {
    out << "## Cache Diagnostics\n";
    for (const auto& diag : model.cache_diagnostics) {
        out << "- " << diag.cache_status << ": " << diag.count << "\n";
    }
    out << "\n";
}

void write_setup_deep_dive(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Setup Deep Dive\n";
    out << "*Note: in chunked export mode, per-worker setup costs are summed in the aggregate.*\n\n";
    out << "| Sub-Phase | Duration |\n";
    out << "| --- | --- |\n";
    out << "| Graph parsing | " << format_ms(run.setup_graph_parsing_ms) << " |\n";
    out << "| Asset I/O load | " << format_ms(run.setup_asset_io_load_ms) << " |\n";
    out << "| Pool preallocation | " << format_ms(run.setup_pool_preallocation_ms) << " |\n";
    out << "| Image decode | " << format_ms(run.image_decode_ms) << " |\n\n";
}

} // namespace chronon3d::cli::report_sections
#endif
