#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_system_resources(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    (void)model;
    out << "## System Resources & Utilization\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";

    if (analysis.logical_cores > 0)
        out << "| Logical cores | " << analysis.logical_cores << " |\n";
    if (analysis.ram_total_mb > 0)
        out << "| RAM total | " << analysis.ram_total_mb << " MB |\n";
    if (analysis.ram_avail_min_mb > 0)
        out << "| RAM available (minimum during run) | " << analysis.ram_avail_min_mb << " MB |\n";
    if (analysis.rss_peak_mb > 0)
        out << "| Process RSS peak | " << analysis.rss_peak_mb << " MB |\n";

    if (analysis.cpu_total_ms > 0 && model.run.wall_time_ms > 0 && analysis.logical_cores > 0) {
        out << "| Process CPU user | " << format_ms(analysis.cpu_user_ms) << " |\n";
        out << "| Process CPU sys | " << format_ms(analysis.cpu_sys_ms) << " |\n";
        out << "| Process CPU total | " << format_ms(analysis.cpu_total_ms) << " |\n";
        out << "| Effective cores used | " << fmt::format("{:.2f}", analysis.effective_cores) << " / " << analysis.logical_cores << " |\n";
        out << "| CPU utilization | " << fmt::format("{:.1f}%", analysis.cpu_util_pct) << " |\n";
    }

    if (analysis.tbb_arena > 0)
        out << "| TBB arena max concurrency | " << analysis.tbb_arena << " |\n";
    if (analysis.tbb_peak > 0)
        out << "| TBB active workers peak | " << analysis.tbb_peak << " workers |\n";
    if (analysis.tbb_avg_count > 0) {
        const double tbb_avg = static_cast<double>(analysis.tbb_avg_sum) / static_cast<double>(analysis.tbb_avg_count);
        out << "| TBB active workers avg | " << fmt::format("{:.2f}", tbb_avg) << " |\n";
    }
    if (analysis.parallel_count > 0 || analysis.parallel_skipped > 0) {
        out << "| Parallel regions executed | " << analysis.parallel_count << " |\n";
        out << "| Parallel regions skipped (≤1 node) | " << analysis.parallel_skipped << " |\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
