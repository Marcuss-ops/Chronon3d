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
