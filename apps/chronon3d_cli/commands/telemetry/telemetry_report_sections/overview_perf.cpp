#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_overview(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Overview\n";
    out << "| Field | Value |\n";
    out << "| --- | --- |\n";
    out << "| Composition | " << run.composition_id << " |\n";
    out << "| Run ID | " << model.run_id << " |\n";
    out << "| Status | " << (run.success ? "SUCCESS" : "FAILED") << " |\n";
    out << "| Finished At | " << run.finished_at_iso << " |\n";
    out << "| Output | " << run.output_path << " |\n";
    out << "| Git Commit | " << run.git_commit_short << " |\n";
    out << "| Build | " << run.build_type << " |\n";
    out << "| OS / CPU | " << run.os << " / " << run.cpu_model << " (" << run.cores << " cores) |\n\n";
}

void write_performance(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    const auto& run = model.run;
    out << "## Performance\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Effective FPS | " << fmt::format("{:.2f} fps", run.effective_fps) << " |\n";
    out << "| Wall Duration | " << fmt::format("{:.2f} s", run.wall_time_ms / 1000.0) << " |\n";
    out << "| Render Duration | " << fmt::format("{:.2f} s", run.render_ms / 1000.0) << " |\n";
    out << "| Encoder Close + Flush Duration | " << fmt::format("{:.2f} s", run.encode_ms / 1000.0) << " |\n";
    out << "| Peak Memory | " << format_bytes(run.bytes_allocated_peak) << " |\n";
    out << "| Node Cache Hit Rate | " << format_pct(analysis.cache_hit_rate) << " |\n";
    out << "| Frames Total | " << run.frames_total << " |\n";
    out << "| Frames Written | " << run.frames_written << " |\n\n";
}

} // namespace chronon3d::cli::report_sections
#endif
