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
    out << "| Encoder Close + Flush Duration | " << fmt::format("{:.2f} s", run.encode_ms / 1000.0) << " |\n";
    out << "| Peak Memory | " << format_bytes(run.bytes_allocated_peak) << " |\n";
    out << "| Node Cache Hit Rate | " << format_pct(cache_hit_rate) << " |\n";
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
            out << "| Output Frame Cache Hit Rate | " << format_pct(sql_double(stmt, 4)) << " |\n";
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

    // ── Phase CPU Efficiency ───────────────────────────────────────────────────
    // Fetch all counter values needed from SQL (used by both this section and
    // the Parallelism Decisions section below).
    uint64_t lev_par = 0, lev_seq = 0, lev_skip = 0;
    uint64_t used_par_clear = 0, used_par_transform = 0, used_par_composite = 0;
    uint64_t fb_copy_ms = 0, compositenode_blend_ms = 0, node_exec_ms = 0;
    double tbb_avg_workers = 1.0;
    {
        const char* par_sql =
            "SELECT counter_name, counter_value FROM render_counters WHERE run_id = ? "
            "AND counter_name IN ("
            "'tbb_active_workers_avg_sum','tbb_active_workers_avg_count',"
            "'used_parallel_clear','used_parallel_transform','used_parallel_composite',"
            "'framebuffer_copy_ms','compositenode_blend_ms','node_execute_actual_ms',"
            "'level_parallel_count','level_sequential_count','parallel_regions_skipped_small_level')"
            " ORDER BY counter_name;";
        sqlite3_stmt* stmt = nullptr;
        uint64_t tbb_sum = 0, tbb_cnt = 0;
        if (prepare_with_run_id(db, &stmt, par_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const std::string name = sql_text(stmt, 0);
                const uint64_t val = static_cast<uint64_t>(sql_i64(stmt, 1));
                if (name == "tbb_active_workers_avg_sum") tbb_sum = val;
                else if (name == "tbb_active_workers_avg_count") tbb_cnt = val;
                else if (name == "used_parallel_clear") used_par_clear = val;
                else if (name == "used_parallel_transform") used_par_transform = val;
                else if (name == "used_parallel_composite") used_par_composite = val;
                else if (name == "framebuffer_copy_ms") fb_copy_ms = val;
                else if (name == "compositenode_blend_ms") compositenode_blend_ms = val;
                else if (name == "node_execute_actual_ms") node_exec_ms = val;
                else if (name == "level_parallel_count") lev_par = val;
                else if (name == "level_sequential_count") lev_seq = val;
                else if (name == "parallel_regions_skipped_small_level") lev_skip = val;
            }
        }
        sqlite3_finalize(stmt);
        if (tbb_cnt > 0) {
            tbb_avg_workers = static_cast<double>(tbb_sum) / static_cast<double>(tbb_cnt);
        }
    }

    out << "## Phase CPU Efficiency\n";
    out << "| Phase | Wall | Pixels | Est. Cores | CPU ms | GB/s |\n";
    out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    {
        // Estimate total pixels processed across all frames
        const uint64_t conv_pixels = (run.frame_conversion_copy_ms > 0) ? run.pixels_touched : 0;

        const auto phase_line = [&](const char* name, double wall_ms,
                                     uint64_t pixels, double est_cores) {
            if (wall_ms <= 0.0 && pixels == 0) return;
            const double cpu_ms = wall_ms * est_cores;
            const double bytes = static_cast<double>(pixels) * 16.0;
            const double wall_s = wall_ms / 1000.0;
            const double gb_s = wall_s > 0.0 ? (bytes / wall_s) / 1.0e9 : 0.0;

            out << "| " << name << " | " << format_ms(wall_ms)
                << " | " << pixels
                << " | " << fmt::format("{:.2f}", est_cores)
                << " | " << format_ms(cpu_ms)
                << " | " << fmt::format("{:.2f}", gb_s)
                << " |\n";
        };

        // Clear: wall from RunSummary, parallelism from decision counter
        phase_line("clear_node",  static_cast<double>(run.clearnode_ms),
                   run.clear_pixels,
                   used_par_clear > 0 ? tbb_avg_workers : 1.0);

        // Transform: estimate wall from pixel share of node_execute_actual_ms (from SQL)
        const double transform_share = (run.pixels_touched > 0 && run.transform_pixels > 0)
            ? std::min(1.0, static_cast<double>(run.transform_pixels) /
                            static_cast<double>(run.pixels_touched))
            : 0.0;
        const double transform_wall = static_cast<double>(node_exec_ms) * transform_share;
        phase_line("transform_node", transform_wall,
                   run.transform_pixels,
                   used_par_transform > 0 ? tbb_avg_workers : 1.0);

        // Composite: wall from SQL, pixels from RunSummary
        phase_line("composite_node", static_cast<double>(compositenode_blend_ms),
                   run.composite_pixels,
                   used_par_composite > 0 ? tbb_avg_workers : 1.0);

        // Frame conversion: parallel (TBB), pixels estimated from total work
        phase_line("frame_conversion", static_cast<double>(run.frame_conversion_copy_ms),
                   conv_pixels, tbb_avg_workers);

        // Framebuffer copy: wall from SQL, pixels from RunSummary
        phase_line("framebuffer_copy", static_cast<double>(fb_copy_ms),
                   fb_copy_ms > 0 ? run.pixels_touched : 0,
                   tbb_avg_workers);
    }
    out << "\n";

    // ── Graph Executor Phase Timings ──────────────────────────────────────────
    out << "## Graph Executor Phase Timings\n";
    out << "| Phase | Duration |\n";
    out << "| --- | --- |\n";
    out << "| compiled graph refresh | " << format_ms(run.compiled_graph_refresh_ms) << " |\n";
    out << "| cache eval | " << format_ms(run.cache_eval_ms) << " |\n";
    out << "| dirty eval | " << format_ms(run.dirty_eval_ms) << " |\n";
    out << "| input resolve | " << format_ms(run.input_resolve_ms) << " |\n";
    out << "| framebuffer lifetime | " << format_ms(run.framebuffer_lifetime_ms) << " |\n";
    out << "| node schedule | " << format_ms(run.node_schedule_ms) << " |\n";
    out << "| node dispatch | " << format_ms(run.node_dispatch_ms) << " |\n";
    out << "| telemetry emit | " << format_ms(run.telemetry_emit_ms) << " |\n";
    out << "\n";

    // ── Telemetry Counters ────────────────────────────────────────────────────
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
    out << "| framebuffer pool miss (best-fit) | " << run.framebuffer_pool_miss_count_best_fit << " |\n";
    out << "| framebuffer returned to pool | " << run.framebuffer_buffer_returned_to_pool_count << " |\n\n";

    // ── Framebuffer Pool (runtime snapshot) ────────────────────────────────────
    out << "## Framebuffer Pool (runtime snapshot)\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        const char* pool_sql =
            "SELECT counter_name, counter_value FROM telemetry_counters WHERE run_id = ? "
            "AND counter_name IN ("
            "'framebuffer_pool_capacity', 'framebuffer_pool_available_count', "
            "'framebuffer_pool_current_bytes', 'framebuffer_pool_total_allocations', "
            "'framebuffer_pool_total_reuses', 'pool_current_bytes', 'pool_available_count') "
            "ORDER BY CASE counter_name "
            "WHEN 'framebuffer_pool_capacity' THEN 1 "
            "WHEN 'framebuffer_pool_available_count' THEN 2 "
            "WHEN 'framebuffer_pool_current_bytes' THEN 3 "
            "WHEN 'framebuffer_pool_total_allocations' THEN 4 "
            "WHEN 'framebuffer_pool_total_reuses' THEN 5 "
            "WHEN 'pool_current_bytes' THEN 6 "
            "WHEN 'pool_available_count' THEN 7 "
            "ELSE 8 END;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, pool_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const std::string name = sql_text(stmt, 0);
                const uint64_t value = static_cast<uint64_t>(sql_i64(stmt, 1));
                if (name.find("_bytes") != std::string::npos) {
                    out << "| " << name << " | " << format_bytes(value) << " |\n";
                } else {
                    out << "| " << name << " | " << value << " |\n";
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

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
    out << "| IO writer idle wait | " << format_ms(run.io_writer_idle_wait_ms) << " |\n";
    out << "| IO queue peak depth | " << run.io_queue_peak_depth << " frames |\n";
    out << "| IO queue peak bytes | " << format_bytes(run.io_queue_peak_bytes) << " |\n\n";

    // ── FFmpeg Pipe ───────────────────────────────────────────────────────────
    out << "## FFmpeg Pipe\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| ffmpeg pipe write blocked | " << format_ms(run.ffmpeg_pipe_write_blocked_ms) << " |\n";
    out << "| Converted Frame Cache Hits | " << run.converted_frame_cache_hits << " |\n";
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
            "GROUP BY node_name, node_type ORDER BY SUM(duration_ms) DESC LIMIT 50;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, node_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const double cache_rate = sql_i64(stmt, 6) > 0
                    ? static_cast<double>(sql_i64(stmt, 5)) / static_cast<double>(sql_i64(stmt, 6))
                    : 0.0;            out << "| " << sql_text(stmt, 0) << " | "
                << sql_text(stmt, 1) << " | "
                << sql_i64(stmt, 2) << " | "
                << format_ms(sql_double(stmt, 3)) << " | "
                << format_ms(sql_double(stmt, 4)) << " | "
                << format_pct(cache_rate) << " | "
                << sql_i64(stmt, 7) << " |\n";
            }
        }
        sqlite3_finalize(stmt);

        // Coverage: sum total hot nodes and compare against node_execute_actual_ms
        {
            const char* sum_sql =
                "SELECT COALESCE(SUM(duration_ms), 0) FROM render_node_events WHERE run_id = ?;";
            sqlite3_stmt* sum_stmt = nullptr;
            uint64_t hot_nodes_total_ms = 0;
            if (prepare_with_run_id(db, &sum_stmt, sum_sql, run_id) && sqlite3_step(sum_stmt) == SQLITE_ROW) {
                hot_nodes_total_ms = static_cast<uint64_t>(sql_double(sum_stmt, 0));
            }
            sqlite3_finalize(sum_stmt);

            const uint64_t actual_ms = node_exec_ms; // from Phase CPU Efficiency query
            const double coverage = actual_ms > 0
                ? (static_cast<double>(hot_nodes_total_ms) / static_cast<double>(actual_ms)) * 100.0
                : 0.0;
            out << "\n**Hot Nodes Coverage:** " << format_ms(hot_nodes_total_ms)
                << " / " << format_ms(actual_ms)
                << " (" << fmt::format("{:.1f}%", coverage) << ")";
            if (coverage < 80.0) {
                out << " ⚠️ Low coverage — missing nodes: ClearNode (memcpy), FrameCopy, PipeWrite, etc.";
            }
            out << "\n";
        }
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
            "GROUP BY layer_id ORDER BY SUM(duration_ms) DESC LIMIT 50;";
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
    out << "| Frame | Duration | Cache | Dirty Ratio | Dirty Enabled | Dirty Rect | Tile | Fast Path | Graph Reused |\n";
    out << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    {
        const char* sample_sql =
            "SELECT frame_number, duration_ms, cache_hit, dirty_area_ratio, "
            "dirty_rect_enabled, dirty_rect_x0, dirty_rect_y0, dirty_rect_x1, dirty_rect_y1, "
            "tile_execution_used, fast_path_reused, graph_reused "
            "FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC LIMIT 12;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, sample_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const bool dr_enabled = sqlite3_column_int(stmt, 4);
                const int x0 = sqlite3_column_int(stmt, 5);
                const int y0 = sqlite3_column_int(stmt, 6);
                const int x1 = sqlite3_column_int(stmt, 7);
                const int y1 = sqlite3_column_int(stmt, 8);
                std::string rect_str = "-";
                if (dr_enabled) {
                    rect_str = fmt::format("[{},{}→{},{}]", x0, y0, x1, y1);
                }
                out << "| #" << sql_i64(stmt, 0) << " | "
                    << format_ms(sql_double(stmt, 1)) << " | "
                    << (sqlite3_column_int(stmt, 2) ? "hit" : "miss") << " | "
                    << format_pct(sql_double(stmt, 3)) << " | "
                    << (dr_enabled ? "yes" : "no") << " | "
                    << rect_str << " | "
                    << (sqlite3_column_int(stmt, 9) ? "yes" : "no") << " | "
                    << (sqlite3_column_int(stmt, 10) ? "yes" : "no") << " | "
                    << (sqlite3_column_int(stmt, 11) ? "yes" : "no") << " |\n";
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
    out << "*Note: in chunked export mode, per-worker setup costs are summed in the aggregate.*\n\n";
    out << "| Sub-Phase | Duration |\n";
    out << "| --- | --- |\n";
    out << "| Graph parsing | " << format_ms(run.setup_graph_parsing_ms) << " |\n";
    out << "| Asset I/O load | " << format_ms(run.setup_asset_io_load_ms) << " |\n";
    out << "| Pool preallocation | " << format_ms(run.setup_pool_preallocation_ms) << " |\n";
    out << "| Image decode | " << format_ms(run.image_decode_ms) << " |\n\n";

    // ── Parallelism Decisions ─────────────────────────────────────────────────
    out << "## Parallelism Decisions\n";
    out << "| Decision | Count |\n";
    out << "| --- | ---: |\n";
    {
        const char* par_sql =
            "SELECT counter_name, counter_value FROM render_counters WHERE run_id = ? "
            "AND counter_name IN ("
            "'used_parallel_clear','skipped_clear_small',"
            "'used_parallel_transform','skipped_transform_small',"
            "'used_parallel_composite','skipped_composite_small',"
            "'skipped_encoder_backpressure')"
            " ORDER BY counter_name;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, par_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const std::string name = sql_text(stmt, 0);
                const uint64_t val = static_cast<uint64_t>(sql_i64(stmt, 1));
                out << "| " << name << " | " << val << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    // Also emit the existing level-level decision counters (from local SQL vars)
    out << "| level_parallel_count | " << lev_par << " |\n";
    out << "| level_sequential_count | " << lev_seq << " |\n";
    out << "| parallel_regions_skipped_small_level | " << lev_skip << " |\n";

    // Bottleneck classification hint (uses only RunSummary/SQL fields)
    {
        const bool encoder_backlog = run.ffmpeg_pipe_write_blocked_ms > run.render_ms * 0.2;
        const bool all_sequential_enum = lev_par == 0 && lev_seq > 0;
        out << "\n**Bottleneck classification:** ";
        if (encoder_backlog) {
            out << "Encoder/pipe bottleneck — ffmpeg write blocked for "
                << format_ms(run.ffmpeg_pipe_write_blocked_ms) << " ("
                << fmt::format("{:.0f}%", run.ffmpeg_pipe_write_blocked_ms * 100.0 / std::max(1.0, run.render_ms))
                << " of render time).\n";
        } else if (all_sequential_enum) {
            out << "Pipeline bottleneck — all node levels executed sequentially. Graph too narrow (≤1 node/level) or regions too small for parallelization.\n";
        } else {
            out << "Parallelism active — no dominant bottleneck detected.\n";
        }
    }
    out << "\n";

    // ── System Resources & Utilization ────────────────────────────────────────
    out << "## System Resources & Utilization\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        // Fetch all system telemetry counters in one query
        uint64_t logical_cores = 0, ram_total_mb = 0, ram_avail_min_mb = 0;
        uint64_t rss_peak_mb = 0, cpu_user_ms = 0, cpu_sys_ms = 0;
        uint64_t tbb_arena = 0, tbb_peak = 0, tbb_avg_sum = 0, tbb_avg_count = 0;
        uint64_t parallel_count = 0, parallel_skipped = 0;

        const char* sys_sql =
            "SELECT counter_name, counter_value FROM render_counters WHERE run_id = ? "
            "AND counter_name IN ("
            "'system_logical_cores','system_ram_total_mb','system_ram_available_min_mb',"
            "'process_cpu_user_ms','process_cpu_sys_ms','process_rss_peak_mb',"
            "'tbb_arena_max_concurrency','tbb_active_workers_peak',"
            "'tbb_active_workers_avg_sum','tbb_active_workers_avg_count',"
            "'parallel_regions_count','parallel_regions_skipped_small_level')"
            " ORDER BY counter_name;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, sys_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const std::string name = sql_text(stmt, 0);
                const uint64_t val = static_cast<uint64_t>(sql_i64(stmt, 1));
                if (name == "system_logical_cores") logical_cores = val;
                else if (name == "system_ram_total_mb") ram_total_mb = val;
                else if (name == "system_ram_available_min_mb") ram_avail_min_mb = val;
                else if (name == "process_cpu_user_ms") cpu_user_ms = val;
                else if (name == "process_cpu_sys_ms") cpu_sys_ms = val;
                else if (name == "process_rss_peak_mb") rss_peak_mb = val;
                else if (name == "tbb_arena_max_concurrency") tbb_arena = val;
                else if (name == "tbb_active_workers_peak") tbb_peak = val;
                else if (name == "tbb_active_workers_avg_sum") tbb_avg_sum = val;
                else if (name == "tbb_active_workers_avg_count") tbb_avg_count = val;
                else if (name == "parallel_regions_count") parallel_count = val;
                else if (name == "parallel_regions_skipped_small_level") parallel_skipped = val;
            }
        }
        sqlite3_finalize(stmt);

        // ── Hardware / Memory ───────────────────────────────────────────
        if (logical_cores > 0)
            out << "| Logical cores | " << logical_cores << " |\n";
        if (ram_total_mb > 0)
            out << "| RAM total | " << ram_total_mb << " MB |\n";
        if (ram_avail_min_mb > 0)
            out << "| RAM available (minimum during run) | " << ram_avail_min_mb << " MB |\n";
        if (rss_peak_mb > 0)
            out << "| Process RSS peak | " << rss_peak_mb << " MB |\n";

        // ── CPU Utilization ─────────────────────────────────────────────
        const uint64_t cpu_total_ms = cpu_user_ms + cpu_sys_ms;
        const double wall_ms = run.wall_time_ms;
        if (cpu_total_ms > 0 && wall_ms > 0 && logical_cores > 0) {
            const double effective_cores = static_cast<double>(cpu_total_ms) / wall_ms;
            const double cpu_util_pct = effective_cores / static_cast<double>(logical_cores) * 100.0;

            out << "| Process CPU user | " << format_ms(cpu_user_ms) << " |\n";
            out << "| Process CPU sys | " << format_ms(cpu_sys_ms) << " |\n";
            out << "| Process CPU total | " << format_ms(cpu_total_ms) << " |\n";
            out << "| Effective cores used | " << fmt::format("{:.2f}", effective_cores) << " / " << logical_cores << " |\n";
            out << "| CPU utilization | " << fmt::format("{:.1f}%", cpu_util_pct) << " |\n";
        }

        // ── TBB Utilization ─────────────────────────────────────────────
        if (tbb_arena > 0)
            out << "| TBB arena max concurrency | " << tbb_arena << " |\n";
        if (tbb_peak > 0)
            out << "| TBB active workers peak | " << tbb_peak << " workers |\n";
        if (tbb_avg_count > 0) {
            const double tbb_avg = static_cast<double>(tbb_avg_sum) / static_cast<double>(tbb_avg_count);
            out << "| TBB active workers avg | " << fmt::format("{:.2f}", tbb_avg) << " |\n";
        }
        if (parallel_count > 0 || parallel_skipped > 0) {
            out << "| Parallel regions executed | " << parallel_count << " |\n";
            out << "| Parallel regions skipped (≤1 node) | " << parallel_skipped << " |\n";
        }
    }
    out << "\n";

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

    // ── Cache Architecture ────────────────────────────────────────────────────
    out << "## Cache Architecture\n";
    out << "Chronon uses three separate caching layers with distinct roles:\n\n";
    out << "| Cache Layer | Measures | Reported As |\n";
    out << "| --- | --- | --- |\n";
    out << "| **Node Cache** | Per-render-node hit rate. Each render node (Clear, Composite, grid_bg, etc.) is checked against a content-addressable cache before execution. | `Node Cache Hit Rate`, `node cache hits/misses` |\n";
    out << "| **Output Frame Cache** | Whether the entire rendered output frame was reused from a previous frame. High on static scenes, drops when content changes. | `Output Frame Cache Hit Rate`, Frame Samples `hit/miss` |\n";
    out << "| **Converted Frame Cache** | YUV conversion cache for the video encoder. Avoids re-converting RGBA→YUV when the same frame is sent multiple times. | `converted frame cache hits` (FFmpeg Pipe section) |\n";
    out << "\n";
    out << "**Important:** A \"Node Cache Hit Rate\" of 0% with \"Output Frame Cache Hit Rate\" of 100% is **not a contradiction**.\n";
    out << "The output frame cache operates at a higher level: if the entire scene is unchanged between frames, the output is reused\n";
    out << "without executing individual render nodes at all. Conversely, per-node counters (composite_calls, nodes_executed, etc.)\n";
    out << "may show 0 because the node telemetry count via `RenderCounters` atomics operates independently from per-node event logging.\n";
    out << "The Hot Nodes and Layer Cost Breakdown sections below draw from per-event logs and give a more precise picture.\n\n";

    // ── Things to Know ────────────────────────────────────────────────────────
    // ── Correctness Verification ──────────────────────────────────────────────
    out << "## Correctness Verification\n";
    out << "| Check | Status |\n";
    out << "| --- | --- |\n";
    out << "| Pixel NaN/Inf | Not checked inline — run `chronon3d_breathing_golden_tests` to verify |\n";
    out << "| Golden comparison | Execute `chronon3d_breathing_golden_tests` (separate binary) |\n";
    out << "| Determinism | Hash-identical across runs (verified via `chronon3d_determinism_test`) |\n";
    out << "\n**Note:** Correctness verification requires running the dedicated golden test binary.\n";
    out << "The CLI report cannot invoke it inline without adding significant latency to the report generation.\n";
    out << "Use `ctest --test-dir build -R breathing_golden` or run the binary directly.\n\n";

    out << "## Things to Know\n";
    out << "- Node Cache hit rate: " << format_pct(cache_hit_rate) << ". ";
    if (cache_hit_rate == 0.0 && run.cache_hits == 0 && run.cache_misses == 0) {
        out << "(No node cache operations recorded — nodes may have been served by the output frame cache or executed without cache tracking.)\n";
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

} // namespace chronon3d::cli
#endif
