#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_frame_summary(std::stringstream& out, const ReportModel& model) {
    const auto& summary = model.frame_summary;
    const auto& breakdown = model.active_cached_breakdown;

    out << "## Frame Summary\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Frame Count | " << summary.count << " |\n";
    out << "| Average Frame | " << format_ms(summary.avg_duration_ms) << " |\n";
    out << "| Min Frame | " << format_ms(summary.min_duration_ms) << " |\n";
    out << "| Max Frame | " << format_ms(summary.max_duration_ms) << " |\n";
    out << "| Output Frame Cache Hit Rate | " << format_pct(summary.avg_cache_hit) << " |\n";
    out << "| Average Dirty Area Ratio | " << format_pct(summary.avg_dirty_area_ratio) << " |\n";

    if (breakdown.active_count > 0) {
        out << "| Avg Frame (active) | " << format_ms(breakdown.avg_active_ms)
            << " (" << breakdown.active_count << " frames) |\n";
    }
    if (breakdown.cached_count > 0) {
        out << "| Avg Frame (cached) | " << format_ms(breakdown.avg_cached_ms)
            << " (" << breakdown.cached_count << " frames) |\n";
    }
    if (breakdown.active_count > 0 || breakdown.cached_count > 0) {
        out << "| Frames Graph Executed | " << breakdown.active_count << " |\n";
        out << "| Frames Graph Skipped | " << breakdown.cached_count << " |\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
