import os
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────────────────────
TELEMETRY_DIR = Path(os.path.expanduser('~')) / '.chronon3d' / 'telemetry'
DB_PATH = TELEMETRY_DIR / 'chronon3d_render_history.sqlite'
JSONL_PATH = TELEMETRY_DIR / 'render_history.jsonl'


# ── Utilities ──────────────────────────────────────────────────────────────────────
def parse_iso(value):
    if not value:
        return ''
    return str(value)


# ── MIME types ─────────────────────────────────────────────────────────────────────
ARTIFACT_MIME_TYPES = {
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.jpeg': 'image/jpeg',
    '.webp': 'image/webp',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.mp4': 'video/mp4',
    '.webm': 'video/webm',
    '.mov': 'video/quicktime',
}

STATIC_MIME_TYPES = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
}


# ── Column definitions (matching JSONL keys → DB columns) ─────────────────────────
RUN_COLUMNS = [
    'run_id', 'composition_id', 'output_path', 'success', 'error_code', 'error_message',
    'frames_total', 'frames_written', 'wall_time_ms', 'render_ms', 'encode_ms',
    'effective_fps', 'pixels_touched', 'cache_hits', 'cache_misses', 'nodes_executed',
    'layers_rendered', 'text_glyphs_rasterized', 'images_sampled', 'blur_pixels',
    'simd_lerp_calls', 'tiles_total', 'tiles_hit', 'tiles_miss', 'tiles_partial',
    'bytes_allocated_peak', 'node_cache_hash_collisions',
    'clear_calls', 'clear_pixels', 'composite_calls', 'composite_pixels',
    'transform_calls', 'transform_pixels', 'effect_stack_calls', 'effect_pixels',
    'layer_culling_tests', 'layers_culled', 'layers_visible',
    'framebuffer_allocations', 'framebuffer_reuses', 'framebuffer_bytes_allocated',
    'framebuffer_bytes_peak',
    'dirty_rect_count', 'dirty_pixels', 'dirty_full_fallbacks',
    'started_at_iso', 'finished_at_iso', 'git_commit_short', 'build_type',
    'compiler_info', 'os', 'cpu_model', 'cores',
]

FRAME_COLUMNS = ['run_id', 'frame_number', 'duration_ms', 'cache_hit', 'dirty_area_ratio']

PHASE_COLUMNS = ['run_id', 'phase_name', 'duration_ms']

COUNTER_COLUMNS = ['run_id', 'counter_name', 'counter_value']

NODE_COLUMNS = [
    'run_id', 'frame_number', 'node_name', 'node_type', 'layer_id', 'duration_ms',
    'cache_status', 'cache_key_digest', 'input_count', 'output_width', 'output_height',
    'output_bytes', 'bbox_x', 'bbox_y', 'bbox_w', 'bbox_h',
    'visible_x', 'visible_y', 'visible_w', 'visible_h',
    'pixels_touched', 'pixels_cleared', 'pixels_composited', 'pixels_transformed', 'pixels_blurred',
]

LAYER_COLUMNS = [
    'run_id', 'frame_number', 'layer_id', 'layer_name', 'layer_type', 'duration_ms',
    'visible', 'cull_reason', 'opacity', 'blend_mode',
    'bbox_x', 'bbox_y', 'bbox_w', 'bbox_h',
    'visible_x', 'visible_y', 'visible_w', 'visible_h',
    'area_pixels', 'visible_pixels', 'dirty_pixels', 'effects',
    'effect_padding', 'glyphs_rasterized', 'images_sampled',
]

CACHE_COLUMNS = [
    'run_id', 'frame_number', 'node_name', 'cacheable', 'cache_status',
    'key_digest', 'params_hash', 'source_hash', 'input_hash', 'output_bytes',
]

CULLING_COLUMNS = [
    'run_id', 'frame_number', 'layer_id', 'visible', 'reason',
    'bbox_x', 'bbox_y', 'bbox_w', 'bbox_h',
    'visible_x', 'visible_y', 'visible_w', 'visible_h',
    'saved_pixels',
]

TEXT_COLUMNS = [
    'run_id', 'frame_number', 'layer_id',
    'text_length', 'line_count', 'glyph_count', 'glyphs_rasterized',
    'glyph_cache_hits', 'glyph_cache_misses',
    'layout_ms', 'raster_ms', 'composite_ms',
    'font_path', 'font_size',
]

IMAGE_COLUMNS = [
    'run_id', 'frame_number', 'layer_id',
    'image_path', 'image_width', 'image_height', 'cache_status',
    'decode_ms', 'sample_ms', 'sampled_pixels',
]

TILE_COLUMNS = [
    'run_id', 'frame_number', 'layer_id', 'tile_x', 'tile_y',
    'tile_status', 'dirty_rects_count',
]

# All table names for bulk-loading from the on-disk DB
ALL_TABLES = [
    'render_runs', 'render_frames', 'render_phase_events', 'render_counters',
    'render_node_events', 'render_layer_events', 'render_cache_events',
    'render_culling_events', 'render_text_events', 'render_image_events',
    'render_tile_events',
]
