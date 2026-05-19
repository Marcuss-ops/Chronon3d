-- Chronon3D Telemetry Pre-built SQL Analysis Queries

-- 1. Run Summary Stats
-- Summary metrics for each render execution ordered by completion time.
SELECT 
    run_id,
    composition_id,
    success,
    frames_total,
    frames_written,
    wall_time_ms,
    render_ms,
    encode_ms,
    effective_fps,
    (bytes_allocated_peak / 1024.0 / 1024.0) AS peak_mem_mb,
    finished_at_iso
FROM render_runs
ORDER BY finished_at_iso DESC;

-- 2. Cache Efficiency Metrics
-- Calculates cache hit/miss rates, tiles efficiency, and dirty area ratios.
SELECT 
    run_id,
    composition_id,
    cache_hits,
    cache_misses,
    CAST(cache_hits AS REAL) / (cache_hits + cache_misses) * 100.0 AS node_cache_hit_rate_pct,
    tiles_hit,
    tiles_miss,
    CAST(tiles_hit AS REAL) / (tiles_hit + tiles_miss) * 100.0 AS tiles_cache_hit_rate_pct,
    node_cache_hash_collisions
FROM render_runs
WHERE (cache_hits + cache_misses) > 0;

-- 3. Node Execution Bottlenecks
-- Ranks graph node executions by total execution time across all frames in a run.
SELECT 
    run_id,
    REPLACE(phase_name, 'node:', '') AS node_name,
    duration_ms AS total_node_dur_ms,
    duration_ms / (SELECT duration_ms FROM render_phase_events WHERE run_id = p.run_id AND phase_name = 'rendering_loop') * 100.0 AS percentage_of_loop_pct
FROM render_phase_events p
WHERE phase_name LIKE 'node:%'
ORDER BY duration_ms DESC;

-- 4. Slowest Frame Analysis
-- Find the top 10 slowest frames rendered across all runs.
SELECT 
    f.run_id,
    r.composition_id,
    f.frame_number,
    f.duration_ms,
    f.cache_hit,
    f.dirty_area_ratio
FROM render_frames f
JOIN render_runs r ON f.run_id = r.run_id
ORDER BY f.duration_ms DESC;

-- 5. Per-Node Execution Breakdown (NEW)
-- Shows which nodes cost the most time per run, with type and cache status.
-- Helps answer: "which node type is the bottleneck?"
SELECT 
    ne.run_id,
    ne.node_type,
    ne.node_name,
    COUNT(*) AS executions,
    SUM(ne.duration_ms) AS total_dur_ms,
    AVG(ne.duration_ms) AS avg_dur_ms,
    SUM(ne.pixels_touched) AS total_pixels_touched,
    SUM(CASE WHEN ne.cache_status = 'hit' THEN 1 ELSE 0 END) AS cache_hits,
    SUM(CASE WHEN ne.cache_status = 'miss' THEN 1 ELSE 0 END) AS cache_misses
FROM render_node_events ne
GROUP BY ne.run_id, ne.node_type, ne.node_name
ORDER BY total_dur_ms DESC;

-- 6. Per-Layer Cost Breakdown (NEW)
-- Human-readable layer-by-layer cost analysis.
-- Helps answer: "which layer is the most expensive?"
SELECT 
    le.run_id,
    le.layer_id,
    le.layer_name,
    le.layer_type,
    le.duration_ms,
    le.visible,
    le.cull_reason,
    le.area_pixels,
    le.visible_pixels,
    le.effects,
    le.glyphs_rasterized,
    le.images_sampled
FROM render_layer_events le
ORDER BY le.duration_ms DESC;

-- 7. Layer Visibility / Culling Summary (NEW)
-- Summary statistics for layer visibility and culling.
SELECT
    run_id,
    SUM(CASE WHEN visible THEN 1 ELSE 0 END) AS layers_visible,
    SUM(CASE WHEN NOT visible THEN 1 ELSE 0 END) AS layers_culled,
    SUM(area_pixels) AS total_area_pixels,
    SUM(visible_pixels) AS total_visible_pixels
FROM render_layer_events
GROUP BY run_id;

-- 8. Cache Diagnostics by Node Type (NEW)
-- Identifies which node types have poor cache behavior.
SELECT
    ne.run_id,
    ne.node_type,
    COUNT(*) AS total,
    SUM(CASE WHEN ne.cache_status = 'hit' THEN 1 ELSE 0 END) AS hits,
    SUM(CASE WHEN ne.cache_status = 'miss' THEN 1 ELSE 0 END) AS misses,
    SUM(CASE WHEN ne.cache_status LIKE 'bypass%' THEN 1 ELSE 0 END) AS bypasses,
    CASE WHEN COUNT(*) > 0
        THEN CAST(SUM(CASE WHEN ne.cache_status = 'hit' THEN 1 ELSE 0 END) AS REAL) /
             COUNT(*) * 100.0
        ELSE 0.0
    END AS hit_rate_pct
FROM render_node_events ne
GROUP BY ne.run_id, ne.node_type
ORDER BY misses DESC;

-- 9. Frame-by-Frame Node Cost (NEW)
-- Detailed per-frame breakdown for bottleneck analysis.
SELECT
    ne.run_id,
    ne.frame_number,
    ne.node_name,
    ne.node_type,
    ne.duration_ms,
    ne.cache_status,
    ne.output_width,
    ne.output_height,
    ne.output_bytes
FROM render_node_events ne
ORDER BY ne.run_id, ne.frame_number, ne.duration_ms DESC;
