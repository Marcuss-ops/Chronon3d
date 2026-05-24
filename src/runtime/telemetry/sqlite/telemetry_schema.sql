CREATE TABLE IF NOT EXISTS render_runs (
    run_id TEXT PRIMARY KEY,
    composition_id TEXT,
    output_path TEXT,
    success INTEGER,
    error_code TEXT,
    error_message TEXT,
    frames_total INTEGER,
    frames_written INTEGER,
    wall_time_ms REAL,
    render_ms REAL,
    encode_ms REAL,
    effective_fps REAL,
    pixels_touched INTEGER,
    cache_hits INTEGER,
    cache_misses INTEGER,
    nodes_executed INTEGER,
    layers_rendered INTEGER,
    text_glyphs_rasterized INTEGER,
    images_sampled INTEGER,
    blur_pixels INTEGER,
    simd_lerp_calls INTEGER,
    tiles_total INTEGER,
    tiles_hit INTEGER,
    tiles_miss INTEGER,
    tiles_partial INTEGER,
    bytes_allocated_peak INTEGER,
    node_cache_hash_collisions INTEGER,
    clear_calls INTEGER,
    clear_pixels INTEGER,
    composite_calls INTEGER,
    composite_pixels INTEGER,
    transform_calls INTEGER,
    transform_pixels INTEGER,
    effect_stack_calls INTEGER,
    effect_pixels INTEGER,
    layer_culling_tests INTEGER,
    layers_culled INTEGER,
    layers_visible INTEGER,
    framebuffer_allocations INTEGER,
    framebuffer_reuses INTEGER,
    framebuffer_bytes_allocated INTEGER,
    framebuffer_bytes_peak INTEGER,
    dirty_rect_count INTEGER,
    dirty_pixels INTEGER,
    dirty_union_area_pixels INTEGER,
    dirty_full_fallbacks INTEGER,
    bypass_not_cacheable_count INTEGER,
    dirty_full_fallback_predicted_bounds_missing INTEGER,
    dirty_full_fallback_composite_missing_input_bounds INTEGER,
    dirty_full_fallback_transform_bounds_unknown INTEGER,
    dirty_full_fallback_effect_bounds_unknown INTEGER,
    chronon_render_only_ms REAL DEFAULT 0,
    chronon_conversion_copy_ms REAL DEFAULT 0,
    chronon_queue_wait_ms REAL DEFAULT 0,
    chronon_render_throughput_ms REAL DEFAULT 0,
    ffmpeg_encode_total_ms REAL DEFAULT 0,
    ffmpeg_flush_close_ms REAL DEFAULT 0,
    e2e_wall_ms REAL DEFAULT 0,
    started_at_iso TEXT,
    finished_at_iso TEXT,
    git_commit_short TEXT,
    build_type TEXT,
    compiler_info TEXT,
    os TEXT,
    cpu_model TEXT,
    cores INTEGER
);

CREATE TABLE IF NOT EXISTS render_frames (
    run_id TEXT,
    frame_number INTEGER,
    duration_ms REAL,
    cache_hit INTEGER,
    dirty_area_ratio REAL,
    PRIMARY KEY (run_id, frame_number)
);

CREATE TABLE IF NOT EXISTS render_phase_events (
    run_id TEXT,
    phase_name TEXT,
    duration_ms REAL,
    PRIMARY KEY (run_id, phase_name)
);

CREATE TABLE IF NOT EXISTS render_counters (
    run_id TEXT,
    counter_name TEXT,
    counter_value INTEGER,
    PRIMARY KEY (run_id, counter_name)
);

CREATE TABLE IF NOT EXISTS render_node_events (
    run_id TEXT,
    frame_number INTEGER,
    node_name TEXT,
    node_type TEXT,
    layer_id TEXT,
    duration_ms REAL,
    cache_status TEXT,
    cache_key_digest TEXT,
    input_count INTEGER,
    output_width INTEGER,
    output_height INTEGER,
    output_bytes INTEGER,
    bbox_x REAL,
    bbox_y REAL,
    bbox_w REAL,
    bbox_h REAL,
    visible_x REAL,
    visible_y REAL,
    visible_w REAL,
    visible_h REAL,
    pixels_touched INTEGER,
    pixels_cleared INTEGER,
    pixels_composited INTEGER,
    pixels_transformed INTEGER,
    pixels_blurred INTEGER,
    PRIMARY KEY (run_id, frame_number, node_name, node_type)
);

CREATE TABLE IF NOT EXISTS render_layer_events (
    run_id TEXT,
    frame_number INTEGER,
    layer_id TEXT,
    layer_name TEXT,
    layer_type TEXT,
    duration_ms REAL,
    visible INTEGER,
    cull_reason TEXT,
    opacity REAL,
    blend_mode TEXT,
    bbox_x REAL,
    bbox_y REAL,
    bbox_w REAL,
    bbox_h REAL,
    visible_x REAL,
    visible_y REAL,
    visible_w REAL,
    visible_h REAL,
    area_pixels INTEGER,
    visible_pixels INTEGER,
    dirty_pixels INTEGER,
    effects TEXT,
    effect_padding REAL,
    glyphs_rasterized INTEGER,
    images_sampled INTEGER,
    PRIMARY KEY (run_id, frame_number, layer_id)
);

CREATE TABLE IF NOT EXISTS render_cache_events (
    run_id TEXT,
    frame_number INTEGER,
    node_name TEXT,
    cacheable INTEGER,
    cache_status TEXT,
    key_digest TEXT,
    params_hash TEXT,
    source_hash TEXT,
    input_hash TEXT,
    output_bytes INTEGER,
    PRIMARY KEY (run_id, frame_number, node_name)
);

CREATE TABLE IF NOT EXISTS render_culling_events (
    run_id TEXT,
    frame_number INTEGER,
    layer_id TEXT,
    visible INTEGER,
    reason TEXT,
    bbox_x REAL,
    bbox_y REAL,
    bbox_w REAL,
    bbox_h REAL,
    visible_x REAL,
    visible_y REAL,
    visible_w REAL,
    visible_h REAL,
    saved_pixels INTEGER,
    PRIMARY KEY (run_id, frame_number, layer_id)
);

CREATE TABLE IF NOT EXISTS render_text_events (
    run_id TEXT,
    frame_number INTEGER,
    layer_id TEXT,
    text_length INTEGER,
    line_count INTEGER,
    glyph_count INTEGER,
    glyphs_rasterized INTEGER,
    glyph_cache_hits INTEGER,
    glyph_cache_misses INTEGER,
    layout_ms REAL,
    raster_ms REAL,
    composite_ms REAL,
    font_path TEXT,
    font_size REAL
);

CREATE TABLE IF NOT EXISTS render_image_events (
    run_id TEXT,
    frame_number INTEGER,
    layer_id TEXT,
    image_path TEXT,
    image_width INTEGER,
    image_height INTEGER,
    cache_status TEXT,
    decode_ms REAL,
    sample_ms REAL,
    sampled_pixels INTEGER
);

CREATE TABLE IF NOT EXISTS render_tile_events (
    run_id TEXT,
    frame_number INTEGER,
    layer_id TEXT,
    tile_x INTEGER,
    tile_y INTEGER,
    tile_status TEXT,
    dirty_rects_count INTEGER,
    PRIMARY KEY (run_id, frame_number, layer_id, tile_x, tile_y)
);

-- Performance indices for analytical queries
CREATE INDEX IF NOT EXISTS idx_node_events_run_type ON render_node_events(run_id, node_type);
CREATE INDEX IF NOT EXISTS idx_node_events_run_duration ON render_node_events(run_id, duration_ms DESC);
CREATE INDEX IF NOT EXISTS idx_cache_events_run_status ON render_cache_events(run_id, cache_status);
CREATE INDEX IF NOT EXISTS idx_frames_run ON render_frames(run_id);
CREATE INDEX IF NOT EXISTS idx_layer_events_run ON render_layer_events(run_id);
CREATE INDEX IF NOT EXISTS idx_culling_events_run ON render_culling_events(run_id);
CREATE INDEX IF NOT EXISTS idx_text_events_run ON render_text_events(run_id);
CREATE INDEX IF NOT EXISTS idx_image_events_run ON render_image_events(run_id);
CREATE INDEX IF NOT EXISTS idx_tile_events_run ON render_tile_events(run_id);
