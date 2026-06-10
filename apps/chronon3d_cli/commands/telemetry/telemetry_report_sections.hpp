#pragma once

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include "telemetry_report_model.hpp"
#include "telemetry_report_analyzer.hpp"
#include <sstream>
#include <string>

namespace chronon3d::cli::report_sections {

void write_overview(std::stringstream& out, const ReportModel& model);
void write_performance(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_frame_summary(std::stringstream& out, const ReportModel& model);
void write_core_render_phases(std::stringstream& out, const ReportModel& model);
void write_phase_cpu_efficiency(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_graph_executor_phases(std::stringstream& out, const ReportModel& model);
void write_telemetry_counters(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_render_metrics(std::stringstream& out, const ReportModel& model);
void write_framebuffer_metrics(std::stringstream& out, const ReportModel& model);
void write_framebuffer_pool(std::stringstream& out, const ReportModel& model);
void write_clear_metrics(std::stringstream& out, const ReportModel& model);
void write_conversion_copy(std::stringstream& out, const ReportModel& model);
void write_queue_metrics(std::stringstream& out, const ReportModel& model);
void write_ffmpeg_pipe(std::stringstream& out, const ReportModel& model);
void write_hot_nodes(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_hot_work_attribution(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_layer_cost_breakdown(std::stringstream& out, const ReportModel& model);
void write_frame_samples(std::stringstream& out, const ReportModel& model);
void write_cache_diagnostics(std::stringstream& out, const ReportModel& model);
void write_setup_deep_dive(std::stringstream& out, const ReportModel& model);
void write_parallelism_decisions(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_system_resources(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_bottleneck_diagnosis(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);
void write_os_process_diagnostics(std::stringstream& out, const ReportModel& model);
void write_cache_architecture(std::stringstream& out);
void write_correctness_verification(std::stringstream& out);
void write_things_to_know(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis);

} // namespace chronon3d::cli::report_sections
#endif
