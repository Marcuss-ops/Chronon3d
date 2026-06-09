#pragma once

// ── SQL Queries for Telemetry Report Generation ─────────────────────────
// All queries take a single ? parameter: the run_id.
// Extracted from command_telemetry_report.cpp for modularity.

namespace chronon3d::cli::telemetry_queries {

/// Frame-level aggregates: count, avg/min/max duration, cache rate, dirty ratio.
inline constexpr const char* kFrameSummary = R"(
SELECT COUNT(*), AVG(duration_ms), MIN(duration_ms), MAX(duration_ms),
       AVG(COALESCE(cache_hit, 0)), AVG(COALESCE(dirty_area_ratio, 1.0))
FROM render_frames WHERE run_id = ?;
)";

/// Phase durations grouped by name, ordered by total descending.
inline constexpr const char* kCoreRenderPhases = R"(
SELECT phase_name, SUM(duration_ms) AS total_ms
FROM render_phase_events WHERE run_id = ?
GROUP BY phase_name ORDER BY total_ms DESC;
)";

/// Performance-critical counter values for Phase CPU Efficiency.
inline constexpr const char* kPhaseCpuEfficiency = R"(
SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?
AND counter_name IN (
'tbb_active_workers_avg_sum','tbb_active_workers_avg_count',
'used_parallel_clear','used_parallel_transform','used_parallel_composite',
'framebuffer_copy_ms','compositenode_blend_ms','node_execute_actual_ms',
'level_parallel_count','level_sequential_count','parallel_regions_skipped_small_level')
ORDER BY counter_name;
)";

/// Hot node event details with cache hit rate.
inline constexpr const char* kHotNodes = R"(
SELECT node_name, node_type, COUNT(*), SUM(duration_ms), AVG(duration_ms),
       SUM(CASE WHEN cache_status='hit' THEN 1 ELSE 0 END),
       SUM(CASE WHEN cache_status IN ('hit','miss') THEN 1 ELSE 0 END),
       SUM(pixels_touched)
FROM render_node_events WHERE run_id = ?
GROUP BY node_name, node_type ORDER BY SUM(duration_ms) DESC LIMIT 50;
)";

/// Sub-node phase costs (ClearNode memcpy/clear, Composite blend/copy/setup, etc.).
inline constexpr const char* kPhaseCostCounters = R"(
SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?
AND counter_name IN (
'clearnode_memcpy_ms','clearnode_clear_ms','clearnode_restore_ms',
'framebuffer_copy_ms',
'compositenode_blend_ms','compositenode_copy_ms','compositenode_setup_ms',
'frame_conversion_copy_ms','video_pipe_write_ms')
ORDER BY counter_name;
)";

/// Sum of all node event durations (for coverage calculation).
inline constexpr const char* kHotNodesCoverageSum = R"(
SELECT COALESCE(SUM(duration_ms), 0) FROM render_node_events WHERE run_id = ?;
)";

/// Attribution counters for Hot Work Attribution section.
inline constexpr const char* kHotWorkAttribution = R"(
SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?
AND counter_name IN (
'clearnode_memcpy_ms','clearnode_clear_ms','clearnode_restore_ms',
'compositenode_blend_ms',
'frame_conversion_copy_ms','video_pipe_write_ms')
ORDER BY counter_name;
)";

/// Single counter: bytes avoided by ClearNode COW.
inline constexpr const char* kBytesAvoided = R"(
SELECT counter_value FROM render_counters WHERE run_id = ?
AND counter_name = 'clearnode_bytes_avoided';
)";

/// Layer event details with pixel/glyph/image statistics.
inline constexpr const char* kLayerCostBreakdown = R"(
SELECT layer_name, layer_type, COUNT(*), SUM(duration_ms), SUM(visible_pixels), SUM(dirty_pixels),
       SUM(glyphs_rasterized), SUM(images_sampled)
FROM render_layer_events WHERE run_id = ?
GROUP BY layer_id ORDER BY SUM(duration_ms) DESC LIMIT 50;
)";

/// Active vs cached frame breakdown: averages and counts for frames
/// where the graph was executed (fast_path_reused=0) vs skipped (fast_path_reused=1).
/// Returns 4 columns: avg_active_ms, active_count, avg_cached_ms, cached_count.
inline constexpr const char* kFrameActiveCachedBreakdown = R"(
SELECT
  COALESCE(AVG(CASE WHEN fast_path_reused = 0 THEN duration_ms END), 0.0),
  COUNT(CASE WHEN fast_path_reused = 0 THEN 1 END),
  COALESCE(AVG(CASE WHEN fast_path_reused = 1 THEN duration_ms END), 0.0),
  COUNT(CASE WHEN fast_path_reused = 1 THEN 1 END)
FROM render_frames WHERE run_id = ?;
)";

/// Per-frame samples with dirty-rect and fast-path flags.
inline constexpr const char* kFrameSamples = R"(
SELECT frame_number, duration_ms, cache_hit, dirty_area_ratio,
       graph_eval_ms, queue_wait_ms, conversion_copy_ms, encoder_ms, pipe_write_ms,
       native_convert_ms, native_send_ms, native_receive_ms, native_mux_ms,
       dirty_rect_enabled, dirty_rect_x0, dirty_rect_y0, dirty_rect_x1, dirty_rect_y1,
       tile_execution_used, fast_path_reused, graph_reused
FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC LIMIT 12;
)";

/// Cache status distribution.
inline constexpr const char* kCacheDiagnostics = R"(
SELECT cache_status, COUNT(*) FROM render_cache_events WHERE run_id = ?
GROUP BY cache_status ORDER BY COUNT(*) DESC;
)";

/// Parallelism decision counters (used/skipped per node type).
inline constexpr const char* kParallelismDecisions = R"(
SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?
AND counter_name IN (
'used_parallel_clear','skipped_clear_small',
'used_parallel_transform','skipped_transform_small',
'used_parallel_composite','skipped_composite_small',
'skipped_encoder_backpressure')
ORDER BY counter_name;
)";

/// System resource counters for CPU, RAM, TBB utilization.
inline constexpr const char* kSystemResources = R"(
SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?
AND counter_name IN (
'system_logical_cores','system_ram_total_mb','system_ram_available_min_mb',
'process_cpu_user_ms','process_cpu_sys_ms','process_rss_peak_mb',
'tbb_arena_max_concurrency','tbb_active_workers_peak',
'tbb_active_workers_avg_sum','tbb_active_workers_avg_count',
'parallel_regions_count','parallel_regions_skipped_small_level')
ORDER BY counter_name;
)";

/// Framebuffer pool runtime snapshot from telemetry_counters table.
inline constexpr const char* kFramebufferPool = R"(
SELECT counter_name, counter_value FROM telemetry_counters WHERE run_id = ?
AND counter_name IN (
'framebuffer_pool_capacity', 'framebuffer_pool_available_count',
'framebuffer_pool_current_bytes', 'framebuffer_pool_total_allocations',
'framebuffer_pool_total_reuses', 'pool_current_bytes', 'pool_available_count')
ORDER BY CASE counter_name
WHEN 'framebuffer_pool_capacity' THEN 1
WHEN 'framebuffer_pool_available_count' THEN 2
WHEN 'framebuffer_pool_current_bytes' THEN 3
WHEN 'framebuffer_pool_total_allocations' THEN 4
WHEN 'framebuffer_pool_total_reuses' THEN 5
WHEN 'pool_current_bytes' THEN 6
WHEN 'pool_available_count' THEN 7
ELSE 8 END;
)";

/// Coverage counters for Bottleneck Diagnosis hot attribution.
inline constexpr const char* kBottleneckCoverage = R"(
SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?
AND counter_name IN (
'clearnode_memcpy_ms','clearnode_clear_ms','clearnode_restore_ms',
'compositenode_blend_ms')
ORDER BY counter_name;
)";

} // namespace chronon3d::cli::telemetry_queries
