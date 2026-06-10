#include "telemetry_report_markdown.hpp"
#include "telemetry_report_sections.hpp"
#include <sstream>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli {

void generate_markdown_report(std::ostream& out, const ReportModel& model, const AnalysisResult& analysis) {
    std::stringstream ss;
    ss << "# Chronon3D Telemetry Report - " << model.run_id << "\n\n";

    using namespace report_sections;
    write_overview(ss, model);
    write_performance(ss, model, analysis);
    write_frame_summary(ss, model);
    write_core_render_phases(ss, model);
    write_phase_cpu_efficiency(ss, model, analysis);
    write_graph_executor_phases(ss, model);
    write_telemetry_counters(ss, model, analysis);
    write_render_metrics(ss, model);
    write_framebuffer_metrics(ss, model);
    write_framebuffer_pool(ss, model);
    write_clear_metrics(ss, model);
    write_conversion_copy(ss, model);
    write_queue_metrics(ss, model);
    write_ffmpeg_pipe(ss, model);
    write_hot_nodes(ss, model, analysis);
    write_hot_work_attribution(ss, model, analysis);
    write_layer_cost_breakdown(ss, model);
    write_frame_samples(ss, model);
    write_cache_diagnostics(ss, model);
    write_setup_deep_dive(ss, model);
    write_parallelism_decisions(ss, model, analysis);
    write_system_resources(ss, model, analysis);
    write_bottleneck_diagnosis(ss, model, analysis);
    write_os_process_diagnostics(ss, model);
    write_cache_architecture(ss);
    write_correctness_verification(ss);
    write_things_to_know(ss, model, analysis);

    out << ss.str();
}

} // namespace chronon3d::cli
#endif
