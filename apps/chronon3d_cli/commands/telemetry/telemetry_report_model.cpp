#include "telemetry_report_model.hpp"
#include "telemetry_queries.hpp"
#include "command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>

namespace chronon3d::cli {

ReportModel load_report_model(sqlite3* db, const std::string& run_id, const RunSummary& run) {
    ReportModel model;
    model.run_id = run_id;
    model.run = run;

    // 1. Frame Summary
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kFrameSummary, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            model.frame_summary.count = sql_i64(stmt, 0);
            model.frame_summary.avg_duration_ms = sql_double(stmt, 1);
            model.frame_summary.min_duration_ms = sql_double(stmt, 2);
            model.frame_summary.max_duration_ms = sql_double(stmt, 3);
            model.frame_summary.avg_cache_hit = sql_double(stmt, 4);
            model.frame_summary.avg_dirty_area_ratio = sql_double(stmt, 5);
        }
        sqlite3_finalize(stmt);
    }

    // 2. Active vs cached frame breakdown
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kFrameActiveCachedBreakdown, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            model.active_cached_breakdown.avg_active_ms = sql_double(stmt, 0);
            model.active_cached_breakdown.active_count = sql_i64(stmt, 1);
            model.active_cached_breakdown.avg_cached_ms = sql_double(stmt, 2);
            model.active_cached_breakdown.cached_count = sql_i64(stmt, 3);
        }
        sqlite3_finalize(stmt);
    }

    // 3. Core Render Phases
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kCoreRenderPhases, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.core_render_phases.push_back({
                    sql_text(stmt, 0),
                    sql_double(stmt, 1)
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 4. Phase CPU Efficiency
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kPhaseCpuEfficiency, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.phase_cpu_efficiency_counters.push_back({
                    sql_text(stmt, 0),
                    static_cast<uint64_t>(sql_i64(stmt, 1))
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 5. Hot Nodes
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kHotNodes, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.hot_nodes.push_back({
                    sql_text(stmt, 0),
                    sql_text(stmt, 1),
                    sql_i64(stmt, 2),
                    sql_double(stmt, 3),
                    sql_double(stmt, 4),
                    sql_i64(stmt, 5),
                    sql_i64(stmt, 6),
                    sql_i64(stmt, 7)
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 6. Phase Cost Counters
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kPhaseCostCounters, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.phase_cost_counters.push_back({
                    sql_text(stmt, 0),
                    static_cast<uint64_t>(sql_i64(stmt, 1))
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 7. Hot Nodes Coverage Sum
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kHotNodesCoverageSum, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            model.hot_nodes_total_ms = static_cast<uint64_t>(sql_double(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    // 8. Hot Work Attribution
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kHotWorkAttribution, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.hot_work_attribution_counters.push_back({
                    sql_text(stmt, 0),
                    static_cast<uint64_t>(sql_i64(stmt, 1))
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 9. Bytes Avoided
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kBytesAvoided, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            model.bytes_avoided = static_cast<uint64_t>(sql_i64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    // 10. Layer Cost Breakdown
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kLayerCostBreakdown, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.layer_cost_breakdown.push_back({
                    sql_text(stmt, 0),
                    sql_text(stmt, 1),
                    sql_i64(stmt, 2),
                    sql_double(stmt, 3),
                    sql_i64(stmt, 4),
                    sql_i64(stmt, 5),
                    sql_i64(stmt, 6),
                    sql_i64(stmt, 7)
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 11. Frame Samples
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kFrameSamples, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                FrameSampleData sample;
                sample.frame_number = sql_i64(stmt, 0);
                sample.duration_ms = sql_double(stmt, 1);
                sample.cache_hit = sqlite3_column_int(stmt, 2) != 0;
                sample.dirty_area_ratio = sql_double(stmt, 3);
                sample.graph_eval_ms = sql_double(stmt, 4);
                sample.queue_wait_ms = sql_double(stmt, 5);
                sample.conversion_copy_ms = sql_double(stmt, 6);
                sample.encoder_ms = sql_double(stmt, 7);
                sample.pipe_write_ms = sql_double(stmt, 8);
                sample.native_convert_ms = sql_double(stmt, 9);
                sample.native_send_ms = sql_double(stmt, 10);
                sample.native_receive_ms = sql_double(stmt, 11);
                sample.native_mux_ms = sql_double(stmt, 12);
                sample.dirty_rect_enabled = sqlite3_column_int(stmt, 13) != 0;
                sample.x0 = sqlite3_column_int(stmt, 14);
                sample.y0 = sqlite3_column_int(stmt, 15);
                sample.x1 = sqlite3_column_int(stmt, 16);
                sample.y1 = sqlite3_column_int(stmt, 17);
                sample.tile_execution_used = sqlite3_column_int(stmt, 18) != 0;
                sample.fast_path_reused = sqlite3_column_int(stmt, 19) != 0;
                sample.graph_reused = sqlite3_column_int(stmt, 20) != 0;
                model.frame_samples.push_back(sample);
            }
        }
        sqlite3_finalize(stmt);
    }

    // 12. Cache Diagnostics
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kCacheDiagnostics, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.cache_diagnostics.push_back({
                    sql_text(stmt, 0),
                    sql_i64(stmt, 1)
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 13. Parallelism Decisions
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kParallelismDecisions, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.parallelism_decisions.push_back({
                    sql_text(stmt, 0),
                    static_cast<uint64_t>(sql_i64(stmt, 1))
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 14. System Resources
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kSystemResources, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.system_resources.push_back({
                    sql_text(stmt, 0),
                    static_cast<uint64_t>(sql_i64(stmt, 1))
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    // 15. Framebuffer Pool
    {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, telemetry_queries::kFramebufferPool, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                model.framebuffer_pool.push_back({
                    sql_text(stmt, 0),
                    static_cast<uint64_t>(sql_i64(stmt, 1))
                });
            }
        }
        sqlite3_finalize(stmt);
    }

    return model;
}

} // namespace chronon3d::cli
#endif
