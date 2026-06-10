#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_parallelism_decisions(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    out << "## Parallelism Decisions\n";
    out << "| Decision | Count |\n";
    out << "| --- | ---: |\n";

    for (const auto& decision : model.parallelism_decisions) {
        out << "| " << decision.name << " | " << decision.value << " |\n";
    }

    out << "| level_parallel_count | " << analysis.lev_par << " |\n";
    out << "| level_sequential_count | " << analysis.lev_seq << " |\n";
    out << "| parallel_regions_skipped_small_level | " << analysis.lev_skip << " |\n";

    // Bottleneck classification hint
    const bool encoder_backlog = model.run.ffmpeg_pipe_write_blocked_ms > model.run.render_ms * 0.2;
    const bool all_sequential_enum = analysis.lev_par == 0 && analysis.lev_seq > 0;
    out << "\n**Bottleneck classification:** ";
    if (encoder_backlog) {
        out << "Encoder/pipe bottleneck — ffmpeg write blocked for "
            << format_ms(model.run.ffmpeg_pipe_write_blocked_ms) << " ("
            << fmt::format("{:.0f}%", model.run.ffmpeg_pipe_write_blocked_ms * 100.0 / std::max(1.0, model.run.render_ms))
            << " of render time).\n";
    } else if (all_sequential_enum) {
        out << "Pipeline bottleneck — all node levels executed sequentially. Graph too narrow (≤1 node/level) or regions too small for parallelization.\n";
    } else {
        out << "Parallelism active — no dominant bottleneck detected.\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
