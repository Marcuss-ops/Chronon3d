#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_frame_samples(std::stringstream& out, const ReportModel& model) {
    out << "## Frame Samples\n";
    out << "| Frame | Duration | Cache | Dirty Ratio | Dirty Enabled | Dirty Rect | Tile | Fast Path | Graph Reused |\n";
    out << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";

    for (const auto& sample : model.frame_samples) {
        std::string rect_str = "-";
        if (sample.dirty_rect_enabled) {
            rect_str = fmt::format("[{},{}→{},{}]", sample.x0, sample.y0, sample.x1, sample.y1);
        }
        out << "| #" << sample.frame_number << " | "
            << format_ms(sample.duration_ms) << " | "
            << (sample.cache_hit ? "hit" : "miss") << " | "
            << format_pct(sample.dirty_area_ratio) << " | "
            << (sample.dirty_rect_enabled ? "yes" : "no") << " | "
            << rect_str << " | "
            << (sample.tile_execution_used ? "yes" : "no") << " | "
            << (sample.fast_path_reused ? "yes" : "no") << " | "
            << (sample.graph_reused ? "yes" : "no") << " |\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
