export const API_BASE = 'http://localhost:8000';

export const QUERY_PRESETS = [
  {
    name: 'Run Summary Stats',
    query: `SELECT 
    run_id,
    composition_id,
    success,
    frames_total,
    frames_written,
    wall_time_ms,
    render_ms,
    effective_fps,
    (bytes_allocated_peak / 1024.0 / 1024.0) AS peak_mem_mb,
    finished_at_iso
FROM render_runs
ORDER BY finished_at_iso DESC;`
  },
  {
    name: 'Cache Efficiency',
    query: `SELECT 
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
WHERE (cache_hits + cache_misses) > 0;`
  },
  {
    name: 'Node Bottlenecks (Phase Events)',
    query: `SELECT 
    run_id,
    REPLACE(phase_name, 'node:', '') AS node_name,
    duration_ms AS total_node_dur_ms,
    duration_ms / (SELECT duration_ms FROM render_phase_events WHERE run_id = p.run_id AND phase_name = 'rendering_loop') * 100.0 AS percentage_of_loop_pct
FROM render_phase_events p
WHERE phase_name LIKE 'node:%'
ORDER BY duration_ms DESC;`
  },
  {
    name: 'Slowest Frame Analysis',
    query: `SELECT 
    f.run_id,
    r.composition_id,
    f.frame_number,
    f.duration_ms,
    f.cache_hit,
    f.dirty_area_ratio
FROM render_frames f
JOIN render_runs r ON f.run_id = r.run_id
ORDER BY f.duration_ms DESC
LIMIT 10;`
  },
  {
    name: 'Per-Node Breakdown',
    query: `SELECT 
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
ORDER BY total_dur_ms DESC;`
  },
  {
    name: 'Per-Layer Cost Breakdown',
    query: `SELECT 
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
ORDER BY le.duration_ms DESC;`
  },
  {
    name: 'Layer Culling Summary',
    query: `SELECT
    run_id,
    SUM(CASE WHEN visible THEN 1 ELSE 0 END) AS layers_visible,
    SUM(CASE WHEN NOT visible THEN 1 ELSE 0 END) AS layers_culled,
    SUM(area_pixels) AS total_area_pixels,
    SUM(visible_pixels) AS total_visible_pixels
FROM render_layer_events
GROUP BY run_id;`
  },
  {
    name: 'Cache Diagnostics by Node Type',
    query: `SELECT
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
ORDER BY misses DESC;`
  },
  {
    name: 'Frame-by-Frame Node Cost',
    query: `SELECT
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
ORDER BY ne.run_id, ne.frame_number, ne.duration_ms DESC;`
  }
];

export const INFO_DESCRIPTIONS = {
  rendering_loop: 'The main rendering loop executing the rendering graph.',
  setup_renderer: 'Time spent setting up context, pipelines, and assets before rendering.',
  Composite: 'Combines individual layers, masks, and backgrounds to form the final frame.',
  Clear: 'Clears the render target/color buffers.',
  Transform: 'Applies 2D/3D matrix transforms (scale, rotate, translate) to layers/objects.',
  EffectStack: 'Evaluates post-processing effects (glow, blur, color adjustments) on the layer stack.',
  bg: 'Renders the composition background.',
  lbl: 'Text layout and glyph rasterized calculations.',
  top_border: 'Draws boundary styling elements or borders.',
  pixels_touched: 'Total number of pixels processed/written across all frames.',
  cache_hits: 'Number of successful cache hits.',
  cache_misses: 'Number of cache misses causing re-evaluation.',
  nodes_executed: 'Total execution count of render graph nodes.',
  layers_rendered: 'Total number of composition layers processed.',
  text_glyphs_rasterized: 'Number of individual font character glyphs rasterized.',
  blur_pixels: 'Total pixels processed by blur filters.',
  bytes_allocated_peak: 'Peak system memory allocated during the render run.',
  effective_fps: 'Frames processed per second including rendering and encoding.',
  wall_time_ms: 'Total clock time elapsed from start to finish.',
  render_ms: 'Core engine CPU/GPU time spent computing/rasterizing frames.',
  minor_grid: 'Renders the fine grid system layout.',
  major_grid: 'Renders the primary coordinates grid layout.',
  glow_outer: 'Computes glowing particle filters around text or objects.'
};
