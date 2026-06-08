import os
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────────────────────
TELEMETRY_DIR = Path(os.path.expanduser('~')) / '.chronon3d' / 'telemetry'
JSONL_PATH = TELEMETRY_DIR / 'render_history.jsonl'


def resolve_db_path() -> Path:
    env_path = os.environ.get('CHRONON3D_TELEMETRY_PATH', '').strip()
    if env_path:
        base = Path(env_path)
        if base.suffix in {'.db', '.sqlite'}:
            return base
        return base / 'chronon3d_render_history.sqlite'
    return TELEMETRY_DIR / 'chronon3d_render_history.sqlite'


DB_PATH = resolve_db_path()

# Canonical schema file (single source of truth, shared with C++)
SCHEMA_SQL_PATH = (
    Path(__file__).resolve().parent.parent.parent.parent
    / 'src' / 'runtime' / 'telemetry' / 'sqlite' / 'telemetry_schema.sql'
)


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
    'clear_skipped_calls', 'clear_skipped_pixels',
    'clear_calls', 'clear_pixels', 'composite_calls', 'composite_pixels',
    'transform_calls', 'transform_pixels', 'effect_stack_calls', 'effect_pixels',
    'layer_culling_tests', 'layers_culled', 'layers_visible',
    'framebuffer_allocations', 'framebuffer_reuses', 'framebuffer_bytes_allocated',
    'framebuffer_bytes_peak',
    'dirty_rect_count', 'dirty_pixels', 'dirty_union_area_pixels', 'dirty_full_fallbacks',
    'bypass_not_cacheable_count',
    'dirty_full_fallback_predicted_bounds_missing',
    'dirty_full_fallback_composite_missing_input_bounds',
    'dirty_full_fallback_transform_bounds_unknown',
    'dirty_full_fallback_effect_bounds_unknown',
    'framebuffer_acquire_ms', 'framebuffer_clear_ms', 'clearnode_ms',
    'framebuffer_pool_clear_ms', 'framebuffer_enqueue_ms',
    'framebuffer_pool_miss_count_size_mismatch', 'framebuffer_pool_miss_count_empty',
    'framebuffer_pool_miss_count_best_fit', 'framebuffer_pool_hits', 'framebuffer_buffer_returned_to_pool_count',
    'unaligned_memory_copies', 'frame_conversion_copy_ms',
    'video_graph_eval_ms', 'video_conversion_ms', 'video_pipe_write_ms', 'video_ffmpeg_latency_ms',
    'io_queue_push_blocked_ms', 'io_queue_pop_wait_ms', 'io_writer_idle_wait_ms',
    'io_queue_peak_depth', 'ffmpeg_pipe_write_blocked_ms', 'converted_frame_cache_hits', 'ffmpeg_flush_ms',
    'io_queue_peak_bytes', 'setup_graph_parsing_ms', 'setup_asset_io_load_ms',
    'setup_pool_preallocation_ms', 'image_decode_ms',
    'chronon_render_only_ms', 'chronon_conversion_copy_ms', 'chronon_queue_wait_ms',
    'chronon_render_throughput_ms', 'ffmpeg_encode_total_ms', 'ffmpeg_flush_close_ms',
    'e2e_wall_ms',
    'started_at_iso', 'finished_at_iso', 'git_commit_short', 'build_type',
    'compiler_info', 'os', 'cpu_model', 'cores',
]

FRAME_COLUMNS = [
    'run_id', 'frame_number', 'duration_ms', 'cache_hit', 'dirty_area_ratio',
    'graph_eval_ms', 'queue_wait_ms', 'conversion_copy_ms', 'encoder_ms', 'pipe_write_ms',
    'native_convert_ms', 'native_send_ms', 'native_receive_ms', 'native_mux_ms',
    'dirty_rect_enabled', 'dirty_rect_x0', 'dirty_rect_y0', 'dirty_rect_x1', 'dirty_rect_y1',
    'tile_execution_used', 'fast_path_reused', 'graph_reused',
]

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
