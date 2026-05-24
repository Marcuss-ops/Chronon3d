#include "command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>
#include <fmt/format.h>
#include <sstream>
#include <string>

namespace chronon3d::cli {

void generate_telemetry_report(std::stringstream& out, sqlite3* db, const std::string& run_id, const RunSummary& run) {
    const double cache_hit_rate = (run.cache_hits + run.cache_misses) > 0
        ? static_cast<double>(run.cache_hits) / static_cast<double>(run.cache_hits + run.cache_misses)
        : 0.0;
    const double framebuffer_reuse_rate = (run.framebuffer_allocations + run.framebuffer_reuses) > 0
        ? static_cast<double>(run.framebuffer_reuses) / static_cast<double>(run.framebuffer_allocations + run.framebuffer_reuses)
        : 0.0;
    double average_dirty_area_ratio = 0.0;
    const double dirty_pixels_share = run.pixels_touched > 0
        ? static_cast<double>(run.dirty_pixels) / static_cast<double>(run.pixels_touched)
        : 0.0;

    out << "# Chronon3D Telemetry Report - " << run_id << "\n\n";

    // ── Overview ──────────────────────────────────────────────────────────────
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

    // ── Performance ───────────────────────────────────────────────────────────
    out << "## Performance\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Effective FPS | " << fmt::format("{:.2f} fps", run.effective_fps) << " |\n";
    out << "| Wall Duration | " << fmt::format("{:.2f} s", run.wall_time_ms / 1000.0) << " |\n";
    out << "| Render Duration | " << fmt::format("{:.2f} s", run.render_ms / 1000.0) << " |\n";
    out << "| Encode Duration | " << fmt::format("{:.2f} s", run.encode_ms / 1000.0) << " |\n";
    out << "| Peak Memory | " << format_bytes(run.bytes_allocated_peak) << " |\n";
    out << "| Cache Hit Rate | " << format_pct(cache_hit_rate) << " |\n";
    out << "| Frames Total | " << run.frames_total << " |\n";
    out << "| Frames Written | " << run.frames_written << " |\n\n";

    // ── Frame Summary ─────────────────────────────────────────────────────────
    out << "## Frame Summary\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        const char* frame_sql =
            "SELECT COUNT(*), AVG(duration_ms), MIN(duration_ms), MAX(duration_ms), "
            "AVG(COALESCE(cache_hit, 0)), AVG(COALESCE(dirty_area_ratio, 1.0)) "
            "FROM render_frames WHERE run_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, frame_sql, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            out << "| Frame Count | " << sql_i64(stmt, 0) << " |\n";
            out << "| Average Frame | " << format_ms(sql_double(stmt, 1)) << " |\n";
            out << "| Min Frame | " << format_ms(sql_double(stmt, 2)) << " |\n";
            out << "| Max Frame | " << format_ms(sql_double(stmt, 3)) << " |\n";
            out << "| Frame Cache Hit Rate | " << format_pct(sql_double(stmt, 4)) << " |\n";
            average_dirty_area_ratio = sql_double(stmt, 5);
            out << "| Average Dirty Area Ratio | " << format_pct(average_dirty_area_ratio) << " |\n";
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    // ── Core Render Phases ────────────────────────────────────────────────────
    out << "## Core Render Phases\n";
    out << "| Phase | Duration |\n";
    out << "| --- | --- |\n";
    {
        const char* phase_sql =
            "SELECT phase_name, SUM(duration_ms) AS total_ms "
            "FROM render_phase_events WHERE run_id = ? "
            "GROUP BY phase_name ORDER BY total_ms DESC;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, phase_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "| " << sql_text(stmt, 0) << " | " << format_ms(sql_double(stmt, 1)) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    // ── Telemetry Counters ────────────────────────────────────────────────────
    out << "## Telemetry Counters\n";
    out << "| Counter | Value |\n";
    out << "| --- | --- |\n";
    out << "| pixels touched | " << run.pixels_touched << " |\n";
    out << "| cache hits | " << run.cache_hits << " |\n";
    out << "| cache misses | " << run.cache_misses << " |\n";
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

    // ── Render ────────────────────────────────────────────────────────────────
    out << "## Render\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| clearnode | " << format_ms(run.clearnode_ms) << " |\n";
    out << "| video graph eval | " << format_ms(run.video_graph_eval_ms) << " |\n\n";

    // ── Framebuffer ───────────────────────────────────────────────────────────
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
    out << "| framebuffer returned to pool | " << run.framebuffer_buffer_returned_to_pool_count << " |\n\n";

    // ── Clear ─────────────────────────────────────────────────────────────────
    out << "## Clear\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| clear calls (graph) | " << run.clear_calls << " |\n";
    out << "| clear pixels (graph) | " << run.clear_pixels << " |\n";
    out << "| clear skipped calls | " << run.clear_skipped_calls << " |\n";
    out << "| clear skipped pixels | " << run.clear_skipped_pixels << " |\n\n";

    // ── Conversion / Copy ─────────────────────────────────────────────────────
    out << "## Conversion / Copy\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| frame conversion copy | " << format_ms(run.frame_conversion_copy_ms) << " |\n";
    out << "| video conversion | " << format_ms(run.video_conversion_ms) << " |\n";
    out << "| video pipe write | " << format_ms(run.video_pipe_write_ms) << " |\n";
    out << "| unaligned memory copies | " << run.unaligned_memory_copies << " |\n\n";

    // ── Queue ─────────────────────────────────────────────────────────────────
    out << "## Queue\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| IO queue push blocked | " << format_ms(run.io_queue_push_blocked_ms) << " |\n";
    out << "| IO queue pop wait | " << format_ms(run.io_queue_pop_wait_ms) << " |\n";
    out << "| IO queue peak depth | " << run.io_queue_peak_depth << " |\n";
    out << "| IO queue peak bytes | " << format_bytes(run.io_queue_peak_bytes) << " |\n\n";

    // ── FFmpeg Pipe ───────────────────────────────────────────────────────────
    out << "## FFmpeg Pipe\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| ffmpeg pipe write blocked | " << format_ms(run.ffmpeg_pipe_write_blocked_ms) << " |\n";
    out << "| converted frame cache hits | " << run.converted_frame_cache_hits << " |\n";
    out << "| ffmpeg flush | " << format_ms(run.ffmpeg_flush_ms) << " |\n";
    out << "| video ffmpeg latency | " << format_ms(run.video_ffmpeg_latency_ms) << " |\n";
    out << "| FFmpeg CPU user | " << run.ffmpeg_cpu_user_pct << "% |\n";
    out << "| FFmpeg CPU sys | " << run.ffmpeg_cpu_sys_pct << "% |\n\n";

    // ── Hot Nodes ─────────────────────────────────────────────────────────────
    out << "## Hot Nodes\n";
    out << "| Node | Type | Calls | Total | Avg | Cache Hit Rate | Pixels Touched |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: |\n";
    {
        const char* node_sql =
            "SELECT node_name, node_type, COUNT(*), SUM(duration_ms), AVG(duration_ms), "
            "SUM(CASE WHEN cache_status='hit' THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN cache_status IN ('hit','miss') THEN 1 ELSE 0 END), "
            "SUM(pixels_touched) "
            "FROM render_node_events WHERE run_id = ? "
            "GROUP BY node_name, node_type ORDER BY SUM(duration_ms) DESC LIMIT 10;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, node_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const double cache_rate = sql_i64(stmt, 6) > 0
                    ? static_cast<double>(sql_i64(stmt, 5)) / static_cast<double>(sql_i64(stmt, 6))
                    : 0.0;
                out << "| " << sql_text(stmt, 0) << " | "
                    << sql_text(stmt, 1) << " | "
                    << sql_i64(stmt, 2) << " | "
                    << format_ms(sql_double(stmt, 3)) << " | "
                    << format_ms(sql_double(stmt, 4)) << " | "
                    << format_pct(cache_rate) << " | "
                    << sql_i64(stmt, 7) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    // ── Layer Cost Breakdown ──────────────────────────────────────────────────
    out << "## Layer Cost Breakdown\n";
    out << "| Layer | Type | Calls | Time | Visible Pixels | Dirty Pixels | Glyphs | Images |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    {
        const char* layer_sql =
            "SELECT layer_name, layer_type, COUNT(*), SUM(duration_ms), SUM(visible_pixels), SUM(dirty_pixels), "
            "SUM(glyphs_rasterized), SUM(images_sampled) "
            "FROM render_layer_events WHERE run_id = ? "
            "GROUP BY layer_id ORDER BY SUM(duration_ms) DESC LIMIT 10;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, layer_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "| " << sql_text(stmt, 0) << " | "
                    << sql_text(stmt, 1) << " | "
                    << sql_i64(stmt, 2) << " | "
                    << format_ms(sql_double(stmt, 3)) << " | "
                    << sql_i64(stmt, 4) << " | "
                    << sql_i64(stmt, 5) << " | "
                    << sql_i64(stmt, 6) << " | "
                    << sql_i64(stmt, 7) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    // ── Frame Samples ─────────────────────────────────────────────────────────
    out << "## Frame Samples\n";
    out << "| Frame | Duration | Cache | Dirty Ratio |\n";
    out << "| --- | --- | --- | --- |\n";
    {
        const char* sample_sql =
            "SELECT frame_number, duration_ms, cache_hit, dirty_area_ratio "
            "FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC LIMIT 12;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, sample_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "| #" << sql_i64(stmt, 0) << " | "
                    << format_ms(sql_double(stmt, 1)) << " | "
                    << (sqlite3_column_int(stmt, 2) ? "hit" : "miss") << " | "
                    << format_pct(sql_double(stmt, 3)) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    // ── Cache Diagnostics ─────────────────────────────────────────────────────
    out << "## Cache Diagnostics\n";
    {
        const char* cache_sql =
            "SELECT cache_status, COUNT(*) FROM render_cache_events WHERE run_id = ? "
            "GROUP BY cache_status ORDER BY COUNT(*) DESC;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, cache_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "- " << sql_text(stmt, 0) << ": " << sql_i64(stmt, 1) << "\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    // ── Setup Deep Dive ───────────────────────────────────────────────────────
    out << "## Setup Deep Dive\n";
    out << "| Sub-Phase | Duration |\n";
    out << "| --- | --- |\n";
    out << "| Graph parsing | " << format_ms(run.setup_graph_parsing_ms) << " |\n";
    out << "| Asset I/O load | " << format_ms(run.setup_asset_io_load_ms) << " |\n";
    out << "| Pool preallocation | " << format_ms(run.setup_pool_preallocation_ms) << " |\n";
    out << "| Image decode | " << format_ms(run.image_decode_ms) << " |\n\n";

    // ── OS & Process Diagnostics ──────────────────────────────────────────────
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

    // ── Things to Know ────────────────────────────────────────────────────────
    out << "## Things to Know\n";
    out << "- Cache hit rate: " << format_pct(cache_hit_rate) << ".\n";
    out << "- Average dirty area ratio: " << format_pct(average_dirty_area_ratio) << " of the frame.\n";
    out << "- Dirty pixels as share of touched pixels: " << format_pct(dirty_pixels_share) << ".\n";
    out << "- Dirty full fallbacks: " << run.dirty_full_fallbacks << ".\n";
    out << "- Framebuffer reuse rate: " << format_pct(framebuffer_reuse_rate) << ".\n";
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

} // namespace chronon3d::cli
#endif
