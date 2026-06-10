#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_graph_executor_phases(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Graph Executor Phase Timings\n";
    out << "| Phase | Duration |\n";
    out << "| --- | --- |\n";
    out << "| compiled graph refresh | " << format_ms(run.compiled_graph_refresh_ms) << " |\n";
    out << "| cache eval | " << format_ms(run.cache_eval_ms) << " |\n";
    out << "| dirty eval | " << format_ms(run.dirty_eval_ms) << " |\n";
    out << "| input resolve | " << format_ms(run.input_resolve_ms) << " |\n";
    out << "| predicted bbox | " << format_ms(run.predicted_bbox_ms) << " |\n";
    out << "| clone context | " << format_ms(run.clone_context_ms) << " |\n";
    out << "| state assign | " << format_ms(run.state_assign_ms) << " |\n";
    out << "| framebuffer lifetime | " << format_ms(run.framebuffer_lifetime_ms) << " |\n";
    out << "| node schedule | " << format_ms(run.node_schedule_ms) << " |\n";
    out << "| node dispatch | " << format_ms(run.node_dispatch_ms) << " |\n";
    out << "| node execute actual | " << format_ms(run.node_execute_actual_ms) << " |\n";
    out << "| node overhead | " << format_ms(run.node_overhead_ms) << " |\n";
    out << "| telemetry emit | " << format_ms(run.telemetry_emit_ms) << " |\n\n";
}

void write_telemetry_counters(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    const auto& run = model.run;
    out << "## Telemetry Counters\n";
    out << "| Counter | Value |\n";
    out << "| --- | --- |\n";
    out << "| pixels touched | " << run.pixels_touched << " |\n";
    out << "| node cache hits | " << run.cache_hits << " |\n";
    out << "| node cache misses | " << run.cache_misses << " |\n";
    out << "| nodes executed | " << run.nodes_executed << " |\n";
    out << "| layers rendered | " << run.layers_rendered << " |\n";
    out << "| text glyphs rasterized | " << run.text_glyphs_rasterized << " |\n";
    out << "| images sampled | " << run.images_sampled << " |\n";
    out << "| blur pixels | " << run.blur_pixels << " |\n";
    out << "| simd lerp calls | " << run.simd_lerp_calls << " |\n";
    out << "| tiles total | " << run.tiles_total << " |\n";
    out << "| tiles hit | " << run.tiles_hit << " |\n";
    out << "| tiles miss | " << run.tiles_miss << " |\n";
    out << "| tiles partial | " << run.tiles_partial << " |\n";
    out << "| node cache hash collisions | " << run.node_cache_hash_collisions << " |\n";
    out << "| clear skipped calls | " << run.clear_skipped_calls << " |\n";
    out << "| clear skipped pixels | " << run.clear_skipped_pixels << " |\n";
    out << "| clear calls | " << run.clear_calls << " |\n";
    out << "| clear pixels | " << run.clear_pixels << " |\n";
    out << "| composite calls | " << run.composite_calls << " |\n";
    out << "| composite pixels | " << run.composite_pixels << " |\n";
    out << "| transform calls | " << run.transform_calls << " |\n";
    out << "| transform pixels | " << run.transform_pixels << " |\n";
    out << "| effect stack calls | " << run.effect_stack_calls << " |\n";
    out << "| effect pixels | " << run.effect_pixels << " |\n";
    out << "| layer culling tests | " << run.layer_culling_tests << " |\n";
    out << "| layers culled | " << run.layers_culled << " |\n";
    out << "| layers visible | " << run.layers_visible << " |\n";
    out << "| framebuffer allocations | " << run.framebuffer_allocations << " |\n";
    out << "| framebuffer reuses | " << run.framebuffer_reuses << " |\n";
    out << "| framebuffer bytes allocated | " << format_bytes(run.framebuffer_bytes_allocated) << " |\n";
    out << "| framebuffer bytes peak | " << format_bytes(run.framebuffer_bytes_peak) << " |\n";
    out << "| dirty rect count | " << run.dirty_rect_count << " |\n";
    out << "| dirty pixels | " << run.dirty_pixels << " |\n";
    out << "| dirty full fallbacks | " << run.dirty_full_fallbacks << " |\n";
    out << "| framebuffer reuse rate | " << format_pct(analysis.framebuffer_reuse_rate) << " |\n\n";
}

void write_render_metrics(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Render\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| clearnode | " << format_ms(run.clearnode_ms) << " |\n";
    out << "| clearnode restore | " << format_ms(run.clearnode_restore_ms) << " |\n";
    out << "| video graph eval | " << format_ms(run.video_graph_eval_ms) << " |\n\n";
}

void write_framebuffer_metrics(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Framebuffer\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| framebuffer acquire | " << format_ms(run.framebuffer_acquire_ms) << " |\n";
    out << "| framebuffer clear | " << format_ms(run.framebuffer_clear_ms) << " |\n";
    out << "| framebuffer pool clear | " << format_ms(run.framebuffer_pool_clear_ms) << " |\n";
    out << "| framebuffer enqueue | " << format_ms(run.framebuffer_enqueue_ms) << " |\n";
    out << "| framebuffer pool exact hit | " << run.framebuffer_pool_exact_hit << " |\n";
    out << "| framebuffer pool empty alloc | " << run.framebuffer_pool_empty_alloc << " |\n";
    out << "| framebuffer pool best-fit reuse | " << run.framebuffer_pool_best_fit_reuse << " |\n";
    out << "| framebuffer returned to pool | " << run.framebuffer_buffer_returned_to_pool_count << " |\n\n";
}

void write_clear_metrics(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Clear\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| clear calls (graph) | " << run.clear_calls << " |\n";
    out << "| clear pixels (graph) | " << run.clear_pixels << " |\n";
    out << "| clear skipped calls | " << run.clear_skipped_calls << " |\n";
    out << "| clear skipped pixels | " << run.clear_skipped_pixels << " |\n\n";
}

void write_conversion_copy(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Conversion / Copy\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| frame conversion copy | " << format_ms(run.frame_conversion_copy_ms) << " |\n";
    out << "| video conversion | " << format_ms(run.video_conversion_ms) << " |\n";
    out << "| video pipe write | " << format_ms(run.video_pipe_write_ms) << " |\n";
    out << "| unaligned memory copies | " << run.unaligned_memory_copies << " |\n\n";
}

void write_queue_metrics(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## Queue\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| IO queue push blocked | " << format_ms(run.io_queue_push_blocked_ms) << " |\n";
    out << "| IO queue pop wait | " << format_ms(run.io_queue_pop_wait_ms) << " |\n";
    out << "| IO writer idle wait | " << format_ms(run.io_writer_idle_wait_ms) << " |\n";
    out << "| IO queue peak depth | " << run.io_queue_peak_depth << " frames |\n";
    out << "| IO queue peak bytes | " << format_bytes(run.io_queue_peak_bytes) << " |\n\n";
}

void write_ffmpeg_pipe(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## FFmpeg Pipe\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| ffmpeg pipe write blocked | " << format_ms(run.ffmpeg_pipe_write_blocked_ms) << " |\n";
    out << "| Converted Frame Cache Hits | " << run.converted_frame_cache_hits << " |\n";
    out << "| ffmpeg flush | " << format_ms(run.ffmpeg_flush_ms) << " |\n";
    out << "| video ffmpeg latency | " << format_ms(run.video_ffmpeg_latency_ms) << " |\n";
    out << "| FFmpeg CPU user | " << run.ffmpeg_cpu_user_pct << "% |\n";
    out << "| FFmpeg CPU sys | " << run.ffmpeg_cpu_sys_pct << "% |\n\n";
}

} // namespace chronon3d::cli::report_sections
#endif
