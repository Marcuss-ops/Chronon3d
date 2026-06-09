#include "command_telemetry_internal.hpp"
#include "telemetry_queries.hpp"
#include "telemetry_report_sections.hpp"

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

    using namespace report_sections;
    write_overview(out, run, run_id);
    write_performance(out, run, cache_hit_rate);

    // ── Frame Summary ─────────────────────────────────────────────────────────
    out << "## Frame Summary\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        const char* frame_sql = telemetry_queries::kFrameSummary;
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

        // ── Active vs cached frame breakdown ────────────────────────────
        // Separates frames where the graph actually executed from those
        // that were entirely skipped (fast-path reuse).  Without this split,
        // the overall average frame time is diluted by the large number of
        // "free" cached frames in long exports, giving a false impression
        // of the true animated render cost.
        {
            const char* ac_sql = telemetry_queries::kFrameActiveCachedBreakdown;
            sqlite3_stmt* ac_stmt = nullptr;
            if (prepare_with_run_id(db, &ac_stmt, ac_sql, run_id) && sqlite3_step(ac_stmt) == SQLITE_ROW) {
                const double avg_active_ms = sql_double(ac_stmt, 0);
                const int64_t active_count = sql_i64(ac_stmt, 1);
                const double avg_cached_ms = sql_double(ac_stmt, 2);
                const int64_t cached_count = sql_i64(ac_stmt, 3);
                if (active_count > 0) {
                    out << "| Avg Frame (active) | " << format_ms(avg_active_ms)
                        << " (" << active_count << " frames) |\n";
                }
                if (cached_count > 0) {
                    out << "| Avg Frame (cached) | " << format_ms(avg_cached_ms)
                        << " (" << cached_count << " frames) |\n";
                }
                if (active_count > 0 || cached_count > 0) {
                    out << "| Frames Graph Executed | " << active_count << " |\n";
                    out << "| Frames Graph Skipped | " << cached_count << " |\n";
                }
            }
            sqlite3_finalize(ac_stmt);
        }
    }
    out << "\n";

    // ── Core Render Phases ────────────────────────────────────────────────────
    out << "## Core Render Phases\n";
    out << "| Phase | Duration |\n";
    out << "| --- | --- |\n";
    {
        const char* phase_sql = telemetry_queries::kCoreRenderPhases;
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
        const char* par_sql = telemetry_queries::kPhaseCpuEfficiency;
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

    write_graph_executor_phases(out, run);
    write_telemetry_counters(out, run, framebuffer_reuse_rate);
    write_render_metrics(out, run);
    write_framebuffer_metrics(out, run);

    // ── Framebuffer Pool (runtime snapshot) ────────────────────────────────────
    out << "## Framebuffer Pool (runtime snapshot)\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        const char* pool_sql = telemetry_queries::kFramebufferPool;
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

    write_clear_metrics(out, run);
    write_conversion_copy(out, run);
    write_queue_metrics(out, run);
    write_ffmpeg_pipe(out, run);

    // ── Hot Nodes ─────────────────────────────────────────────────────────────
    out << "## Hot Nodes\n";
    out << "| Node | Type | Calls | Total | Avg | Cache Hit Rate | Pixels Touched |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: |\n";
    {
        const char* node_sql = telemetry_queries::kHotNodes;
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

        // ── Counter-based phase costs (non-node attribution) ────────────
        // These costs occur inside node execution but are not attributed
        // to specific node names in render_node_events.  Adding them as
        // synthetic rows improves Hot Nodes coverage.
        uint64_t phase_costs_total_ms = 0;
        {
            const char* cost_sql = telemetry_queries::kPhaseCostCounters;
            sqlite3_stmt* cstmt = nullptr;
            if (prepare_with_run_id(db, &cstmt, cost_sql, run_id)) {
                while (sqlite3_step(cstmt) == SQLITE_ROW) {
                    const std::string name = sql_text(cstmt, 0);
                    const uint64_t val = static_cast<uint64_t>(sql_i64(cstmt, 1));
                    if (val == 0) continue;
                    std::string label = name;
                    if (label == "clearnode_memcpy_ms") label = "ClearNode memcpy";
                    else if (label == "clearnode_clear_ms") label = "ClearNode clear";
                    else if (label == "framebuffer_copy_ms") label = "Framebuffer copy";
                    else if (label == "compositenode_blend_ms") label = "Composite blend";
                    else if (label == "compositenode_copy_ms") label = "Composite copy";
                    else if (label == "compositenode_setup_ms") label = "Composite setup";
                    else if (label == "frame_conversion_copy_ms") label = "Frame conversion copy";
                    else if (label == "video_pipe_write_ms") label = "Video pipe write";
                    out << "| " << label << " | Overhead | 1 | "
                        << format_ms(static_cast<double>(val)) << " | "
                        << format_ms(static_cast<double>(val)) << " | — | 0 |\n";
                    phase_costs_total_ms += val;
                }
            }
            sqlite3_finalize(cstmt);
        }

        // Coverage: sum total hot nodes (node events only, not phase-cost
        // breakdown counters — those are sub-costs already inside node times)
        // and compare against node_execute_actual_ms.
        {
            const char* sum_sql = telemetry_queries::kHotNodesCoverageSum;
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
            if (phase_costs_total_ms > 0) {
                out << " — phase breakdown adds " << format_ms(phase_costs_total_ms) << " in sub-costs";
            }
            if (coverage < 80.0) {
                out << " ⚠️ Low coverage — missing: executor overhead, pipeline wait, etc.";
            }
            out << "\n";
        }
    }
    out << "\n";

    // ── Hot Work Attribution ────────────────────────────────────────────────
    // Break down wall-clock time into individual work items with estimated
    // CPU cost, parallelism, and memory bandwidth.
    // Counter values are read from SQL because not all phase costs
    // (clearnode_memcpy_ms, clearnode_clear_ms) are in RunSummary.
    out << "## Hot Work Attribution\n";
    out << "| Work | Wall | Pixels | Est. Cores | CPU ms | GB/s |\n";
    out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    {
        // Query phase-cost counters not available in RunSummary
        uint64_t clr_memcpy_ms = 0, clr_clear_ms = 0;
        uint64_t blend_ms = compositenode_blend_ms; // already from Phase CPU Efficiency query
        uint64_t conv_ms = run.frame_conversion_copy_ms;
        uint64_t pipe_ms = run.video_pipe_write_ms;
        {
            const char* attr_sql = telemetry_queries::kHotWorkAttribution;
            sqlite3_stmt* astmt = nullptr;
            if (prepare_with_run_id(db, &astmt, attr_sql, run_id)) {
                while (sqlite3_step(astmt) == SQLITE_ROW) {
                    const std::string name = sql_text(astmt, 0);
                    const uint64_t val = static_cast<uint64_t>(sql_i64(astmt, 1));
                    if (name == "clearnode_memcpy_ms") clr_memcpy_ms = val;
                    else if (name == "clearnode_clear_ms") clr_clear_ms = val;
                    else if (name == "compositenode_blend_ms") blend_ms = val;
                    else if (name == "frame_conversion_copy_ms") conv_ms = val;
                    else if (name == "video_pipe_write_ms") pipe_ms = val;
                }
            }
            sqlite3_finalize(astmt);
        }

        const auto work_line = [&](const char* name, uint64_t wall_ms,
                                    uint64_t pixels, double est_cores,
                                    const char* gbs_override = nullptr) {
            if (wall_ms == 0 && pixels == 0) return;
            const double w_ms = static_cast<double>(wall_ms);
            const double cpu_ms = w_ms * est_cores;
            const double bytes = static_cast<double>(pixels) * 16.0;
            const double wall_s = w_ms / 1000.0;
            const double gb_s = wall_s > 0.0 ? (bytes / wall_s) / 1.0e9 : 0.0;
            out << "| " << name << " | " << format_ms(w_ms)
                << " | " << pixels
                << " | " << fmt::format("{:.2f}", est_cores)
                << " | " << format_ms(cpu_ms)
                << " | ";
            if (gbs_override) {
                out << gbs_override;
            } else {
                out << fmt::format("{:.2f}", gb_s);
            }
            out << " |\n";
        };

        // Estimate total pixels processed for conversion/pipe work
        const uint64_t conv_pixels = (conv_ms > 0) ? run.pixels_touched : 0;
        // Estimate transform wall from pixel share of node_execute_actual_ms
        const double transform_share = (run.pixels_touched > 0 && run.transform_pixels > 0)
            ? std::min(1.0, static_cast<double>(run.transform_pixels) /
                            static_cast<double>(run.pixels_touched))
            : 0.0;
        const uint64_t transform_wall = static_cast<uint64_t>(
            static_cast<double>(node_exec_ms) * transform_share);

        work_line("ClearNode memcpy", clr_memcpy_ms,
                   run.clearnode_copy_pixels,
                   used_par_clear > 0 ? tbb_avg_workers : 1.0);
        work_line("ClearNode clear", clr_clear_ms,
                   run.clear_pixels,
                   used_par_clear > 0 ? tbb_avg_workers : 1.0);
        work_line("Composite blend", blend_ms,
                   run.composite_pixels,
                   used_par_composite > 0 ? tbb_avg_workers : 1.0);
        work_line("Transform rows", transform_wall,
                   run.transform_pixels,
                   used_par_transform > 0 ? tbb_avg_workers : 1.0);
        work_line("Frame conversion", conv_ms,
                   conv_pixels, tbb_avg_workers);
        // Pipe write is I/O bound, not CPU-bound — show "blocked" instead of GB/s
        work_line("Pipe write", pipe_ms,
                   run.pixels_touched, 1.0, "blocked");

        // Bytes avoided: log how much copy work was saved by COW
        {
            const char* avoid_sql = telemetry_queries::kBytesAvoided;
            sqlite3_stmt* av_stmt = nullptr;
            if (prepare_with_run_id(db, &av_stmt, avoid_sql, run_id) && sqlite3_step(av_stmt) == SQLITE_ROW) {
                const uint64_t avoided = static_cast<uint64_t>(sql_i64(av_stmt, 0));
                if (avoided > 0) {
                    out << "| ClearNode bytes avoided | — | — | — | — | "
                        << format_bytes(avoided) << " |\n";
                }
            }
            sqlite3_finalize(av_stmt);
        }

        // Attribution coverage vs node_execute_actual_ms
        const uint64_t total_attributed =
            clr_memcpy_ms + clr_clear_ms + blend_ms +
            transform_wall + conv_ms + pipe_ms;
        const double coverage_pct = node_exec_ms > 0
            ? (static_cast<double>(total_attributed) / static_cast<double>(node_exec_ms)) * 100.0
            : 0.0;
        out << "\n**Attribution coverage:** " << format_ms(static_cast<double>(total_attributed))
            << " / " << format_ms(node_exec_ms)
            << " (" << fmt::format("{:.1f}%", coverage_pct) << ")";
        if (coverage_pct < 80.0) {
            out << " ⚠️ Missing: executor overhead, small-node dispatch, idle wait";
        }
        out << "\n";
    }
    out << "\n";

    // ── Layer Cost Breakdown ──────────────────────────────────────────────────
    out << "## Layer Cost Breakdown\n";
    out << "| Layer | Type | Calls | Time | Visible Pixels | Dirty Pixels | Glyphs | Images |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    {
        const char* layer_sql = telemetry_queries::kLayerCostBreakdown;
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
        const char* sample_sql = telemetry_queries::kFrameSamples;
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
        const char* cache_sql = telemetry_queries::kCacheDiagnostics;
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, cache_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "- " << sql_text(stmt, 0) << ": " << sql_i64(stmt, 1) << "\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    write_setup_deep_dive(out, run);

    out << "## Image Sampling\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Image sample | " << format_ms(run.image_sample_ms) << " |\n";
    out << "| Image sampled pixels | " << run.image_sampled_pixels << " |\n\n";

    // ── Parallelism Decisions ─────────────────────────────────────────────────
    out << "## Parallelism Decisions\n";
    out << "| Decision | Count |\n";
    out << "| --- | ---: |\n";
    {
        const char* par_sql = telemetry_queries::kParallelismDecisions;
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
    // Declare shared variables here so Bottleneck Diagnosis (below) can use them.
    uint64_t logical_cores = 0, ram_total_mb = 0, ram_avail_min_mb = 0;
    uint64_t rss_peak_mb = 0, cpu_user_ms = 0, cpu_sys_ms = 0, cpu_total_ms = 0;
    uint64_t tbb_arena = 0, tbb_peak = 0, tbb_avg_sum = 0, tbb_avg_count = 0;

    out << "## System Resources & Utilization\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        // Fetch all system telemetry counters in one query
        uint64_t parallel_count = 0, parallel_skipped = 0;

        const char* sys_sql = telemetry_queries::kSystemResources;
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
        cpu_total_ms = cpu_user_ms + cpu_sys_ms;
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

    // ── Bottleneck Diagnosis ──────────────────────────────────────────────
    // Classify each subsystem based on counter thresholds.
    // Reuses variables from System Resources query + RunSummary.
    out << "## Bottleneck Diagnosis\n";
    out << "| Area | Status |\n";
    out << "| --- | --- |\n";
    {
        // ── CPU saturation ────────────────────────────────────────────
        if (cpu_total_ms > 0 && run.wall_time_ms > 0 && logical_cores > 0) {
            const double eff = static_cast<double>(cpu_total_ms) / run.wall_time_ms;
            const double util = eff / static_cast<double>(logical_cores) * 100.0;
            const char* st = (util >= 65.0) ? "GOOD"
                           : (util >= 40.0) ? "MODERATE"
                           : "LIMITED";
            out << "| CPU saturation | " << st
                << " (" << fmt::format("{:.1f}%", util) << " of " << logical_cores << " cores) |\n";
        } else {
            out << "| CPU saturation | UNKNOWN |\n";
        }

        // ── Memory pressure ───────────────────────────────────────────
        if (rss_peak_mb > 0 && ram_avail_min_mb > 0) {
            const double ratio = static_cast<double>(rss_peak_mb) / static_cast<double>(ram_avail_min_mb);
            const char* st = (ratio < 0.25) ? "SAFE"
                           : (ratio < 0.50) ? "MODERATE"
                           : "WARNING";
            out << "| Memory pressure | " << st
                << " (" << rss_peak_mb << " MB RSS / " << ram_avail_min_mb << " MB avail) |\n";
        } else {
            out << "| Memory pressure | UNKNOWN |\n";
        }

        // ── Encoder pipe ──────────────────────────────────────────────
        {
            const double enc_share = run.render_ms > 0
                ? run.ffmpeg_pipe_write_blocked_ms / run.render_ms * 100.0 : 0.0;
            const char* st = (enc_share < 10.0) ? "GOOD"
                           : (enc_share < 25.0) ? "MODERATE BOTTLENECK"
                           : "MAJOR BOTTLENECK";
            out << "| Encoder pipe | " << st
                << " (" << fmt::format("{:.0f}%", enc_share) << " of render time blocked) |\n";
        }

        // ── Graph parallelism ─────────────────────────────────────────
        {
            const char* st = (lev_par > lev_seq) ? "STRONG"
                           : (lev_par > 0 && lev_par == lev_seq) ? "MODERATE"
                           : (lev_seq > lev_par) ? "LIMITED BY DAG"
                           : "UNKNOWN";
            out << "| Graph parallelism | " << st
                << " (" << lev_par << " parallel / " << lev_seq << " sequential levels) |\n";
        }

        // ── Node parallelism ──────────────────────────────────────────
        {
            const bool any_par = (used_par_clear > 0 || used_par_transform > 0 || used_par_composite > 0);
            const char* st = (any_par && tbb_peak >= 4) ? "STRONG"
                           : (any_par) ? "MODERATE"
                           : "WEAK";
            out << "| Node parallelism | " << st
                << " (peak: " << tbb_peak << " workers) |\n";
        }

        // ── Frame conversion ──────────────────────────────────────────
        {
            const double cnv_share = run.render_ms > 0
                ? run.frame_conversion_copy_ms / run.render_ms * 100.0 : 0.0;
            const char* st = (cnv_share < 10.0) ? "GOOD"
                           : (cnv_share < 20.0) ? "MODERATE BOTTLENECK"
                           : "MAJOR BOTTLENECK";
            out << "| Frame conversion | " << st
                << " (" << fmt::format("{:.0f}%", cnv_share) << " of render time) |\n";
        }

        // ── Cache efficiency ──────────────────────────────────────────
        {
            const char* st = (cache_hit_rate >= 0.80) ? "EXCELLENT"
                           : (cache_hit_rate >= 0.50) ? "GOOD"
                           : "MODERATE";
            out << "| Cache efficiency | " << st
                << " (" << format_pct(cache_hit_rate) << ") |\n";
        }

        // ── Dirty rect efficiency ─────────────────────────────────────
        {
            const double dr = (run.pixels_touched > 0)
                ? static_cast<double>(run.dirty_pixels) / static_cast<double>(run.pixels_touched) : 1.0;
            const char* st = (dr <= 0.20) ? "EXCELLENT"
                           : (dr <= 0.50) ? "GOOD"
                           : "MODERATE";
            out << "| Dirty rect efficiency | " << st
                << " (" << format_pct(dr) << " dirty) |\n";
        }

        // ── Hot attribution (reuse clr_memcpy/clr_clear/blend from Hot Work section) ──
        // These vars are scoped inside the Hot Work Attribution block; query SQL locally.
        {
            uint64_t attr_clr_mem = 0, attr_clr_clr = 0, attr_blend = 0;
            uint64_t attr_conv = run.frame_conversion_copy_ms;
            uint64_t attr_pipe = run.video_pipe_write_ms;
            {
                const char* cov_sql = telemetry_queries::kBottleneckCoverage;
                sqlite3_stmt* cs = nullptr;
                if (prepare_with_run_id(db, &cs, cov_sql, run_id)) {
                    while (sqlite3_step(cs) == SQLITE_ROW) {
                        const std::string n = sql_text(cs, 0);
                        const uint64_t v = static_cast<uint64_t>(sql_i64(cs, 1));
                        if (n == "clearnode_memcpy_ms") attr_clr_mem = v;
                        else if (n == "clearnode_clear_ms") attr_clr_clr = v;
                        else if (n == "compositenode_blend_ms") attr_blend = v;
                    }
                }
                sqlite3_finalize(cs);
            }
            const double xf_share = (run.pixels_touched > 0 && run.transform_pixels > 0)
                ? std::min(1.0, static_cast<double>(run.transform_pixels) /
                                static_cast<double>(run.pixels_touched)) : 0.0;
            const uint64_t attr_xform = static_cast<uint64_t>(
                static_cast<double>(node_exec_ms) * xf_share);
            const uint64_t attr_total = attr_clr_mem + attr_clr_clr + attr_blend + attr_xform + attr_conv + attr_pipe;
            const double cov = node_exec_ms > 0
                ? static_cast<double>(attr_total) / static_cast<double>(node_exec_ms) * 100.0 : 0.0;
            const char* st = (cov >= 90.0) ? "EXCELLENT"
                           : (cov >= 80.0) ? "GOOD"
                           : "NEEDS WORK";
            out << "| Hot attribution | " << st
                << " (" << fmt::format("{:.0f}%", cov) << " explained) |\n";
        }

        // ── Primary bottleneck summary ────────────────────────────────
        out << "\n**Primary bottleneck:** ";
        {
            const double enc_share = run.render_ms > 0
                ? run.ffmpeg_pipe_write_blocked_ms / run.render_ms * 100.0 : 0.0;
            const double cnv_share = run.render_ms > 0
                ? run.frame_conversion_copy_ms / run.render_ms * 100.0 : 0.0;
            double eff = 0.0;
            if (cpu_total_ms > 0 && run.wall_time_ms > 0 && logical_cores > 0)
                eff = static_cast<double>(cpu_total_ms) / run.wall_time_ms / static_cast<double>(logical_cores) * 100.0;

            if (enc_share >= 25.0) {
                out << "Encoder pipe (FFmpeg subprocess).\n\n"
                    << "**Recommendation:** Increase encoder queue depth or use native encoder path.\n";
            } else if (cnv_share >= 20.0) {
                out << "Frame conversion (RGBA→YUV).\n\n"
                    << "**Recommendation:** Benchmark Highway SIMD vs libyuv; consider direct YUV path.\n";
            } else if (eff < 40.0 && logical_cores > 0) {
                out << "CPU underutilization.\n\n"
                    << "**Recommendation:** Increase parallelism thresholds or enable frame-level parallelism.\n"
                    << "Do not increase render workers beyond " << (logical_cores > 2 ? logical_cores - 2 : 1)
                    << " to leave margin for OS + encoder.\n";
            } else {
                out << "No single dominant bottleneck — system is well-balanced for this workload.\n";
            }
        }
    }
    out << "\n";

    write_os_process_diagnostics(out, run);
    write_cache_architecture(out);
    write_correctness_verification(out);
    write_things_to_know(out, run, cache_hit_rate, average_dirty_area_ratio, dirty_pixels_share, framebuffer_reuse_rate);
}

} // namespace chronon3d::cli
#endif
