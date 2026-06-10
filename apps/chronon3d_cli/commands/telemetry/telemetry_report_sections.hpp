#pragma once

#include "command_telemetry_internal.hpp"

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <fmt/format.h>
#include <cstdint>
#include <sstream>
#include <string>

// ── Section formatters (no SQL, pure RunSummary formatting) ──────────────
// Uses format_pct/format_bytes/format_ms from command_telemetry_internal.hpp.

namespace chronon3d::cli::report_sections {

inline void write_overview(std::stringstream& out,
                           const chronon3d::telemetry::RenderTelemetryRecord& run,
                           const std::string& run_id) {
    out << "## Overview\n";
    out << "| Field | Value |\n";
    out << "| --- | --- |\n";
    out << "| Composition | " << run.composition_id << " |\n";
    out << "| Run ID | " << run_id << " |\n";
    out << "| Status | " << (run.success ? "SUCCESS" : "FAILED") << " |\n";
    out << "| Finished At | " << run.finished_at_iso << " |\n";
    out << "| Output | " << run.output_path << " |\n";
    out << "| Git Commit | " << run.git_commit_short << " |\n";
    out << "| Build | " << run.build_type << " |\n";
    out << "| OS / CPU | " << run.os << " / " << run.cpu_model << " (" << run.cores << " cores) |\n\n";
}

inline void write_performance(std::stringstream& out,
                              const chronon3d::telemetry::RenderTelemetryRecord& run,
                              double cache_hit_rate) {
    out << "## Performance\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Effective FPS | " << fmt::format("{:.2f} fps", run.effective_fps) << " |\n";
    out << "| Wall Duration | " << fmt::format("{:.2f} s", run.wall_time_ms / 1000.0) << " |\n";
    out << "| Render Duration | " << fmt::format("{:.2f} s", run.render_ms / 1000.0) << " |\n";
    out << "| Encoder Close + Flush Duration | " << fmt::format("{:.2f} s", run.encode_ms / 1000.0) << " |\n";
    out << "| Peak Memory | " << format_bytes(run.bytes_allocated_peak) << " |\n";
    out << "| Node Cache Hit Rate | " << format_pct(cache_hit_rate) << " |\n";
    out << "| Frames Total | " << run.frames_total << " |\n";
    out << "| Frames Written | " << run.frames_written << " |\n\n";
}

inline void write_graph_executor_phases(std::stringstream& out,
                                        const chronon3d::telemetry::RenderTelemetryRecord& run) {
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

inline void write_telemetry_counters(std::stringstream& out,
                                     const chronon3d::telemetry::RenderTelemetryRecord& run,
                                     double framebuffer_reuse_rate) {
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
    out << "| framebuffer reuse rate | " << format_pct(framebuffer_reuse_rate) << " |\n\n";
}

inline void write_render_metrics(std::stringstream& out,
                                 const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## Render\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| clearnode | " << format_ms(run.clearnode_ms) << " |\n";
    out << "| clearnode restore | " << format_ms(run.clearnode_restore_ms) << " |\n";
    out << "| video graph eval | " << format_ms(run.video_graph_eval_ms) << " |\n\n";
}

inline void write_framebuffer_metrics(std::stringstream& out,
                                      const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## Framebuffer\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| framebuffer acquire | " << format_ms(run.framebuffer_acquire_ms) << " |\n";
    out << "| framebuffer clear | " << format_ms(run.framebuffer_clear_ms) << " |\n";
    out << "| framebuffer pool clear | " << format_ms(run.framebuffer_pool_clear_ms) << " |\n";
    out << "| framebuffer enqueue | " << format_ms(run.framebuffer_enqueue_ms) << " |\n";
    out << "| framebuffer pool hits | " << run.framebuffer_pool_hits << " |\n";
    out << "| framebuffer pool miss (size) | " << run.framebuffer_pool_miss_count_size_mismatch << " |\n";
    out << "| framebuffer pool miss (empty) | " << run.framebuffer_pool_miss_count_empty << " |\n";
    out << "| framebuffer pool miss (best-fit) | " << run.framebuffer_pool_miss_count_best_fit << " |\n";
    out << "| framebuffer returned to pool | " << run.framebuffer_buffer_returned_to_pool_count << " |\n\n";
}

inline void write_clear_metrics(std::stringstream& out,
                                const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## Clear\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| clear calls (graph) | " << run.clear_calls << " |\n";
    out << "| clear pixels (graph) | " << run.clear_pixels << " |\n";
    out << "| clear skipped calls | " << run.clear_skipped_calls << " |\n";
    out << "| clear skipped pixels | " << run.clear_skipped_pixels << " |\n\n";
}

inline void write_conversion_copy(std::stringstream& out,
                                  const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## Conversion / Copy\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| frame conversion copy | " << format_ms(run.frame_conversion_copy_ms) << " |\n";
    out << "| video conversion | " << format_ms(run.video_conversion_ms) << " |\n";
    out << "| video pipe write | " << format_ms(run.video_pipe_write_ms) << " |\n";
    out << "| unaligned memory copies | " << run.unaligned_memory_copies << " |\n\n";
}

inline void write_queue_metrics(std::stringstream& out,
                                const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## Queue\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| IO queue push blocked | " << format_ms(run.io_queue_push_blocked_ms) << " |\n";
    out << "| IO queue pop wait | " << format_ms(run.io_queue_pop_wait_ms) << " |\n";
    out << "| IO writer idle wait | " << format_ms(run.io_writer_idle_wait_ms) << " |\n";
    out << "| IO queue peak depth | " << run.io_queue_peak_depth << " frames |\n";
    out << "| IO queue peak bytes | " << format_bytes(run.io_queue_peak_bytes) << " |\n\n";
}

inline void write_ffmpeg_pipe(std::stringstream& out,
                              const chronon3d::telemetry::RenderTelemetryRecord& run) {
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

inline void write_setup_deep_dive(std::stringstream& out,
                                  const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## Setup Deep Dive\n";
    out << "*Note: in chunked export mode, per-worker setup costs are summed in the aggregate.*\n\n";
    out << "| Sub-Phase | Duration |\n";
    out << "| --- | --- |\n";
    out << "| Graph parsing | " << format_ms(run.setup_graph_parsing_ms) << " |\n";
    out << "| Asset I/O load | " << format_ms(run.setup_asset_io_load_ms) << " |\n";
    out << "| Pool preallocation | " << format_ms(run.setup_pool_preallocation_ms) << " |\n";
    out << "| Image decode | " << format_ms(run.image_decode_ms) << " |\n\n";
}

inline void write_os_process_diagnostics(std::stringstream& out,
                                         const chronon3d::telemetry::RenderTelemetryRecord& run) {
    out << "## OS & Process Diagnostics\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Context switches (voluntary) | " << run.process_context_switches_voluntary << " |\n";
    out << "| Context switches (involuntary) | " << run.process_context_switches_involuntary << " |\n";
    out << "| Page faults (major) | " << run.os_page_faults_major << " |\n";
    out << "| Page faults (minor) | " << run.os_page_faults_minor << " |\n";
    out << "| LLC references | " << run.llc_references << " |\n";
    out << "| LLC misses | " << run.llc_misses << " |\n";
    if (run.llc_references > 0) {
        const double llc_miss_rate = static_cast<double>(run.llc_misses) / static_cast<double>(run.llc_references) * 100.0;
        out << "| LLC miss rate | " << format_pct(llc_miss_rate / 100.0) << " |\n";
    }
    out << "\n";
}

inline void write_cache_architecture(std::stringstream& out) {
    out << "## Cache Architecture\n";
    out << "Chronon uses three separate caching layers with distinct roles:\n\n";
    out << "| Cache Layer | Measures | Reported As |\n";
    out << "| --- | --- | --- |\n";
    out << "| **Node Cache** | Per-render-node hit rate. Each render node (Clear, Composite, grid_bg, etc.) is checked against a content-addressable cache before execution. | `Node Cache Hit Rate`, `node cache hits/misses` |\n";
    out << "| **Output Frame Cache** | Whether the entire rendered output frame was reused from a previous frame. High on static scenes, drops when content changes. | `Output Frame Cache Hit Rate`, Frame Samples `hit/miss` |\n";
    out << "| **Converted Frame Cache** | YUV conversion cache for the video encoder. Avoids re-converting RGBA->YUV when the same frame is sent multiple times. | `converted frame cache hits` (FFmpeg Pipe section) |\n";
    out << "\n";
    out << "**Important:** A \"Node Cache Hit Rate\" of 0% with \"Output Frame Cache Hit Rate\" of 100% is **not a contradiction**.\n";
    out << "The output frame cache operates at a higher level: if the entire scene is unchanged between frames, the output is reused\n";
    out << "without executing individual render nodes at all. Conversely, per-node counters (composite_calls, nodes_executed, etc.)\n";
    out << "may show 0 because the node telemetry count via `RenderCounters` atomics operates independently from per-node event logging.\n";
    out << "The Hot Nodes and Layer Cost Breakdown sections below draw from per-event logs and give a more precise picture.\n\n";
}

inline void write_correctness_verification(std::stringstream& out) {
    out << "## Correctness Verification\n";
    out << "| Check | Status |\n";
    out << "| --- | --- |\n";
    out << "| Pixel NaN/Inf | Not checked inline -- run `chronon3d_breathing_golden_tests` to verify |\n";
    out << "| Golden comparison | Execute `chronon3d_breathing_golden_tests` (separate binary) |\n";
    out << "| Determinism | Hash-identical across runs (verified via `chronon3d_determinism_test`) |\n";
    out << "\n**Note:** Correctness verification requires running the dedicated golden test binary.\n";
    out << "Use `ctest --test-dir build -R breathing_golden` or run the binary directly.\n\n";
}

inline void write_things_to_know(std::stringstream& out,
                                 const chronon3d::telemetry::RenderTelemetryRecord& run,
                                 double cache_hit_rate,
                                 double average_dirty_area_ratio,
                                 double dirty_pixels_share,
                                 double framebuffer_reuse_rate) {
    out << "## Things to Know\n";
    out << "- Node Cache hit rate: " << format_pct(cache_hit_rate) << ". ";
    if (cache_hit_rate == 0.0 && run.cache_hits == 0 && run.cache_misses == 0) {
        out << "(No node cache operations recorded -- nodes may have been served by the output frame cache or executed without cache tracking.)\n";
    } else {
        out << "\n";
    }
    out << "- Average dirty area ratio: " << format_pct(average_dirty_area_ratio) << " of the frame.\n";
    out << "- Dirty pixels as share of touched pixels: " << format_pct(dirty_pixels_share) << ".\n";
    out << "- Dirty full fallbacks: " << run.dirty_full_fallbacks << ".\n";
    out << "- Framebuffer reuse rate: " << format_pct(framebuffer_reuse_rate) << ". ";
    if (framebuffer_reuse_rate == 0.0 && run.framebuffer_bytes_peak > 0 && run.framebuffer_allocations == 0) {
        out << "(Framebuffers were likely pre-allocated during warmup; the allocation counters are reset after warmup.)\n";
    } else {
        out << "\n";
    }
    if (run.nodes_executed == 0) {
        out << "- **Note:** `nodes_executed` reads 0 but Hot Nodes may still show calls > 0. ";
        out << "The per-node event log (Hot Nodes below) records execution independently from the `RenderCounters` atomic counter.\n";
    }
    if (run.os_page_faults_major > 0) {
        out << "- **Warning:** Major page faults detected (" << run.os_page_faults_major << "). Setup is hitting disk I/O.\n";
    }
    if (run.process_context_switches_involuntary > run.process_context_switches_voluntary * 2) {
        out << "- **Warning:** High involuntary context switches. CPU contention detected.\n";
    }
    out << "- If render time stays high while cache hit rate is strong, the hot path is likely compositing, clear passes, or framebuffer churn rather than rasterization.\n";
    out << "- If text glyph rasterization is low, text is probably not the main bottleneck anymore; blur/glow and layer recomposition become the next suspects.\n";
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
