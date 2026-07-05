import sqlite3
import threading
from pathlib import Path

from .config import (
    DB_PATH, JSONL_PATH, SCHEMA_SQL_PATH, ALL_TABLES,
    RUN_COLUMNS, FRAME_COLUMNS, PHASE_COLUMNS, COUNTER_COLUMNS,
    NODE_COLUMNS, LAYER_COLUMNS, CACHE_COLUMNS, CULLING_COLUMNS,
    TEXT_COLUMNS, IMAGE_COLUMNS, TILE_COLUMNS,
)
from .jsonl_loader import load_jsonl_records


# ── Schema DDL (read from canonical .sql file shared with C++) ────────────────────
def _load_schema_sql():
    """Read the canonical schema from the shared .sql file."""
    if SCHEMA_SQL_PATH.exists():
        return SCHEMA_SQL_PATH.read_text(encoding='utf-8')
    # Fallback: embedded copy of the canonical schema for when source tree is unavailable
    import logging
    logging.warning("telemetry_schema.sql not found at %s, using embedded fallback", SCHEMA_SQL_PATH)
    return _FALLBACK_SCHEMA


_FALLBACK_SCHEMA = r"""
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
    clear_skipped_calls INTEGER,
    clear_skipped_pixels INTEGER,
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
    framebuffer_acquire_ms INTEGER DEFAULT 0,
    framebuffer_clear_ms INTEGER DEFAULT 0,
    clearnode_ms INTEGER DEFAULT 0,
    clearnode_restore_ms INTEGER DEFAULT 0,
    framebuffer_pool_clear_ms INTEGER DEFAULT 0,
    framebuffer_enqueue_ms INTEGER DEFAULT 0,
    framebuffer_pool_empty_alloc INTEGER DEFAULT 0,
    framebuffer_pool_miss_count_empty INTEGER DEFAULT 0,
    framebuffer_pool_miss_count_best_fit INTEGER DEFAULT 0,
    framebuffer_pool_miss_count_size_mismatch INTEGER DEFAULT 0,
    framebuffer_pool_best_fit_reuse INTEGER DEFAULT 0,
    framebuffer_pool_exact_hit INTEGER DEFAULT 0,
    framebuffer_pool_hits INTEGER DEFAULT 0,
    framebuffer_buffer_returned_to_pool_count INTEGER DEFAULT 0,
    unaligned_memory_copies INTEGER DEFAULT 0,
    frame_conversion_copy_ms INTEGER DEFAULT 0,
    video_graph_eval_ms INTEGER DEFAULT 0,
    video_conversion_ms INTEGER DEFAULT 0,
    video_pipe_write_ms INTEGER DEFAULT 0,
    video_ffmpeg_latency_ms INTEGER DEFAULT 0,
    io_queue_push_blocked_ms INTEGER DEFAULT 0,
    io_queue_pop_wait_ms INTEGER DEFAULT 0,
    io_writer_idle_wait_ms INTEGER DEFAULT 0,
    io_queue_peak_depth INTEGER DEFAULT 0,
    ffmpeg_pipe_write_blocked_ms INTEGER DEFAULT 0,
    converted_frame_cache_hits INTEGER DEFAULT 0,
    ffmpeg_flush_ms INTEGER DEFAULT 0,
    io_queue_peak_bytes INTEGER DEFAULT 0,
    setup_graph_parsing_ms INTEGER DEFAULT 0,
    setup_asset_io_load_ms INTEGER DEFAULT 0,
    setup_pool_preallocation_ms INTEGER DEFAULT 0,
    image_decode_ms INTEGER DEFAULT 0,
    image_sample_ms INTEGER DEFAULT 0,
    image_sampled_pixels INTEGER DEFAULT 0,
    compiled_graph_refresh_ms INTEGER DEFAULT 0,
    cache_eval_ms INTEGER DEFAULT 0,
    dirty_eval_ms INTEGER DEFAULT 0,
    input_resolve_ms INTEGER DEFAULT 0,
    framebuffer_lifetime_ms INTEGER DEFAULT 0,
    node_schedule_ms INTEGER DEFAULT 0,
    node_dispatch_ms INTEGER DEFAULT 0,
    telemetry_emit_ms INTEGER DEFAULT 0,
    predicted_bbox_ms INTEGER DEFAULT 0,
    clone_context_ms INTEGER DEFAULT 0,
    state_assign_ms INTEGER DEFAULT 0,
    node_execute_actual_ms INTEGER DEFAULT 0,
    node_overhead_ms INTEGER DEFAULT 0,

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
    graph_eval_ms REAL DEFAULT 0,
    queue_wait_ms REAL DEFAULT 0,
    conversion_copy_ms REAL DEFAULT 0,
    encoder_ms REAL DEFAULT 0,
    pipe_write_ms REAL DEFAULT 0,
    native_convert_ms REAL DEFAULT 0,
    native_send_ms REAL DEFAULT 0,
    native_receive_ms REAL DEFAULT 0,
    native_mux_ms REAL DEFAULT 0,
    dirty_rect_enabled INTEGER DEFAULT 0,
    dirty_rect_x0 INTEGER DEFAULT 0,
    dirty_rect_y0 INTEGER DEFAULT 0,
    dirty_rect_x1 INTEGER DEFAULT 0,
    dirty_rect_y1 INTEGER DEFAULT 0,
    tile_execution_used INTEGER DEFAULT 0,
    fast_path_reused INTEGER DEFAULT 0,
    graph_reused INTEGER DEFAULT 0,
    program_cache_capacity INTEGER DEFAULT 0,
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
"""


SCHEMA_SQL = _load_schema_sql()


# ── Shared read-only connection wrapper (avoids backup() copy of entire DB) ──────
class SharedReadConnection:
    """Wraps a cached in-memory SQLite connection for safe concurrent reads.

    Instead of copying the entire cached DB via backup() on every request,
    we share the master connection using a lock for serialized reads.
    The close() is a no-op so the cached connection survives.
    """

    def __init__(self, master_conn, lock):
        self._conn = master_conn
        self._lock = lock

    def cursor(self):
        return _LockedCursor(self._conn, self._lock)

    def close(self):
        pass  # Don't close the shared master connection


class _LockedCursor:
    """Cursor wrapper that acquires the shared lock for execute() and releases on fetch().

    Stores the inner sqlite3.Cursor after execute() and delegates fetchall()/fetchone()
    to it, releasing the lock once data is retrieved.

    Supports context manager protocol (with cursor.execute(...) as cur:) and iteration.
    """

    def __init__(self, conn, lock):
        self._conn = conn
        self._lock = lock
        self._cur = None
        self._locked = False

    def execute(self, sql, params=None):
        self._lock.acquire()
        self._locked = True
        try:
            if params:
                self._cur = self._conn.execute(sql, params)
            else:
                self._cur = self._conn.execute(sql)
            return self
        except Exception:
            self._release()
            raise

    def _release(self):
        if self._locked:
            self._locked = False
            try:
                if self._cur:
                    self._cur.close()
            finally:
                self._cur = None
                self._lock.release()

    def fetchall(self):
        try:
            return self._cur.fetchall() if self._cur else []
        finally:
            self._release()

    def fetchone(self):
        try:
            return self._cur.fetchone() if self._cur else None
        finally:
            self._release()

    def __iter__(self):
        if not self._cur:
            self._release()
            return iter([])
        try:
            rows = self._cur.fetchall()
            return iter(rows)
        finally:
            self._release()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self._release()

    def __del__(self):
        self._release()


def _load_existing_db(cursor):
    """Copy data from the on-disk SQLite DB into the in-memory connection.

    Handles legacy column drift between the on-disk SQLite schema and the
    canonical in-memory schema read from telemetry_schema.sql:
      * Legacy column aliases (e.g. `composition` -> `composition_id`) are
        remapped into the canonical column name when present in the in-memory
        schema, otherwise the legacy column is silently dropped.
      * Columns that exist in the on-disk DB but are unknown to the in-memory
        schema are dropped (forward-only — never widen the in-memory schema
        from disk content).
      * When the source schema lacks an explicit `run_id`, a stable synthetic
        run_id is generated per row from the source `rowid` so that downstream
        JOINs in `_hydrate_video_metrics` still match the child tables.
      * `INSERT OR IGNORE` is used so that historical rows with NULL primary
        keys (legacy TEXT PRIMARY KEY allows multiple NULLs) do not abort
        the load with an `IntegrityError`.
    """
    if not DB_PATH.exists():
        return

    source = sqlite3.connect(str(DB_PATH))
    source.row_factory = sqlite3.Row
    try:
        # Forward-only legacy aliases. Extend cautiously as new drift is observed.
        LEGACY_COLUMN_ALIASES = {
            'composition': 'composition_id',
        }

        for table in ALL_TABLES:
            try:
                raw_rows = source.execute(
                    f"SELECT rowid AS _source_rowid, * FROM {table}"
                ).fetchall()
            except Exception:
                raw_rows = []
            if not raw_rows:
                continue

            # Snapshot the in-memory schema columns for this table so we can
            # filter (rather than crash-and-suppress) legacy / extra columns.
            try:
                cursor.execute(f"PRAGMA table_info({table})")
                valid_cols = {row[1] for row in cursor.fetchall()}
            except Exception:
                valid_cols = set()
            if not valid_cols:
                continue

            require_run_id = 'run_id' in valid_cols
            synthetic_seed = 0
            for src_row in raw_rows:
                row_dict = {key: src_row[key] for key in src_row.keys()}

                # Synthesize run_id when source schema lacked one — uses the
                # SQLite rowid so identical legacy rows collapse cleanly.
                if require_run_id and not row_dict.get('run_id'):
                    seed = row_dict.pop('_source_rowid', synthetic_seed)
                    row_dict['run_id'] = f"legacy_{table}_{seed}"
                    synthetic_seed += 1

                # Per-row guard against alias collisions: if a row carries both
                # the legacy column (`composition`) and its canonical twin
                # (`composition_id`), we must not insert the same target column
                # twice or sqlite3 raises `too many columns`.
                seen: set[str] = set()
                insert_cols = []
                values = []
                for col, value in row_dict.items():
                    if col.startswith('_'):
                        continue
                    if col in valid_cols and col not in seen:
                        insert_cols.append(col)
                        seen.add(col)
                        values.append(value)
                    elif col in LEGACY_COLUMN_ALIASES \
                            and LEGACY_COLUMN_ALIASES[col] in valid_cols \
                            and LEGACY_COLUMN_ALIASES[col] not in seen:
                        insert_cols.append(LEGACY_COLUMN_ALIASES[col])
                        seen.add(LEGACY_COLUMN_ALIASES[col])
                        values.append(value)
                    # else: legacy/extra column — silently drop

                if not insert_cols:
                    continue

                placeholders = ', '.join(['?'] * len(insert_cols))
                col_list = ', '.join(insert_cols)
                sql = (
                    f"INSERT OR IGNORE INTO {table} ({col_list}) "
                    f"VALUES ({placeholders})"
                )
                try:
                    cursor.execute(sql, values)
                except Exception:
                    # Per-row failure (e.g. type coercion of legacy string
                    # into a numeric column) — keep loading the rest of the
                    # table instead of aborting the entire DB.
                    continue
    finally:
        source.close()


def _build_run_row(record):
    return [record.get(col) for col in RUN_COLUMNS]


def _build_frame_row(record):
    return [
        record.get('run_id'),
        record.get('frame_number'),
        record.get('duration_ms'),
        int(bool(record.get('cache_hit'))),
        record.get('dirty_area_ratio'),
        record.get('graph_eval_ms') or 0.0,
        record.get('queue_wait_ms') or 0.0,
        record.get('conversion_copy_ms') or 0.0,
        record.get('encoder_ms') or 0.0,
        record.get('pipe_write_ms') or 0.0,
        record.get('native_convert_ms') or 0.0,
        record.get('native_send_ms') or 0.0,
        record.get('native_receive_ms') or 0.0,
        record.get('native_mux_ms') or 0.0,
        int(bool(record.get('dirty_rect_enabled'))),
        record.get('dirty_rect_x0') or 0,
        record.get('dirty_rect_y0') or 0,
        record.get('dirty_rect_x1') or 0,
        record.get('dirty_rect_y1') or 0,
        int(bool(record.get('tile_execution_used'))),
        int(bool(record.get('fast_path_reused'))),
        int(bool(record.get('graph_reused'))),
        record.get('program_cache_capacity') or 0,
    ]


def _build_phase_row(record):
    return [
        record.get('run_id'),
        record.get('phase_name'),
        record.get('duration_ms'),
    ]


def _build_counter_row(record):
    return [
        record.get('run_id'),
        record.get('counter_name'),
        record.get('counter_value'),
    ]


def _build_node_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('node_name'),
        record.get('node_type'),
        record.get('layer_id') or '',
        record.get('duration_ms') or 0.0,
        record.get('cache_status') or 'miss',
        record.get('cache_key_digest') or '',
        record.get('input_count') or 0,
        record.get('output_width') or 0,
        record.get('output_height') or 0,
        record.get('output_bytes') or 0,
        record.get('bbox_x') or 0.0,
        record.get('bbox_y') or 0.0,
        record.get('bbox_w') or 0.0,
        record.get('bbox_h') or 0.0,
        record.get('visible_x') or 0.0,
        record.get('visible_y') or 0.0,
        record.get('visible_w') or 0.0,
        record.get('visible_h') or 0.0,
        record.get('pixels_touched') or 0,
        record.get('pixels_cleared') or 0,
        record.get('pixels_composited') or 0,
        record.get('pixels_transformed') or 0,
        record.get('pixels_blurred') or 0,
    ]


def _build_layer_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('layer_id'),
        record.get('layer_name'),
        record.get('layer_type'),
        record.get('duration_ms') or 0.0,
        int(bool(record.get('visible'))),
        record.get('cull_reason') or '',
        record.get('opacity') or 1.0,
        record.get('blend_mode') or 'Normal',
        record.get('bbox_x') or 0.0,
        record.get('bbox_y') or 0.0,
        record.get('bbox_w') or 0.0,
        record.get('bbox_h') or 0.0,
        record.get('visible_x') or 0.0,
        record.get('visible_y') or 0.0,
        record.get('visible_w') or 0.0,
        record.get('visible_h') or 0.0,
        record.get('area_pixels') or 0,
        record.get('visible_pixels') or 0,
        record.get('dirty_pixels') or 0,
        record.get('effects') or '',
        record.get('effect_padding') or 0.0,
        record.get('glyphs_rasterized') or 0,
        record.get('images_sampled') or 0,
    ]


def _build_cache_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('node_name'),
        int(bool(record.get('cacheable'))),
        record.get('cache_status') or 'miss',
        record.get('key_digest') or '',
        record.get('params_hash') or '',
        record.get('source_hash') or '',
        record.get('input_hash') or '',
        record.get('output_bytes') or 0,
    ]


def _build_culling_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('layer_id'),
        int(bool(record.get('visible'))),
        record.get('reason') or '',
        record.get('bbox_x') or 0.0,
        record.get('bbox_y') or 0.0,
        record.get('bbox_w') or 0.0,
        record.get('bbox_h') or 0.0,
        record.get('visible_x') or 0.0,
        record.get('visible_y') or 0.0,
        record.get('visible_w') or 0.0,
        record.get('visible_h') or 0.0,
        record.get('saved_pixels') or 0,
    ]


def _build_text_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('layer_id'),
        record.get('text_length') or 0,
        record.get('line_count') or 0,
        record.get('glyph_count') or 0,
        record.get('glyphs_rasterized') or 0,
        record.get('glyph_cache_hits') or 0,
        record.get('glyph_cache_misses') or 0,
        record.get('layout_ms') or 0.0,
        record.get('raster_ms') or 0.0,
        record.get('composite_ms') or 0.0,
        record.get('font_path') or '',
        record.get('font_size') or 0.0,
    ]


def _build_image_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('layer_id'),
        record.get('image_path') or '',
        record.get('image_width') or 0,
        record.get('image_height') or 0,
        record.get('cache_status') or 'miss',
        record.get('decode_ms') or 0.0,
        record.get('sample_ms') or 0.0,
        record.get('sampled_pixels') or 0,
    ]


def _build_tile_row(record):
    return [
        record.get('run_id') or record.get('run_id_context') or '',
        record.get('frame_number'),
        record.get('layer_id'),
        record.get('tile_x') or 0,
        record.get('tile_y') or 0,
        record.get('tile_status') or '',
        record.get('dirty_rects_count') or 0,
    ]


# ── Record type → (row_builder, columns, table_name) mapping ──────────────────────
_BUILDERS = {
    'run':          (_build_run_row,    RUN_COLUMNS,    'render_runs'),
    'frame':        (_build_frame_row,  FRAME_COLUMNS,  'render_frames'),
    'phase':        (_build_phase_row,  PHASE_COLUMNS,  'render_phase_events'),
    'counter':      (_build_counter_row, COUNTER_COLUMNS, 'render_counters'),
    'node_event':   (_build_node_row,   NODE_COLUMNS,   'render_node_events'),
    'layer_event':  (_build_layer_row,  LAYER_COLUMNS,  'render_layer_events'),
    'cache_event':  (_build_cache_row,  CACHE_COLUMNS,  'render_cache_events'),
    'culling_event':(_build_culling_row, CULLING_COLUMNS,'render_culling_events'),
    'text_event':   (_build_text_row,   TEXT_COLUMNS,   'render_text_events'),
    'image_event':  (_build_image_row,  IMAGE_COLUMNS,  'render_image_events'),
    'tile_event':   (_build_tile_row,   TILE_COLUMNS,   'render_tile_events'),
}


def _insert_batch(cursor, rows, columns, table):
    """Insert a batch of rows into the given table."""
    if not rows:
        return
    col_list = ', '.join(columns)
    placeholders = ', '.join(['?'] * len(columns))
    sql = f"INSERT OR REPLACE INTO {table} ({col_list}) VALUES ({placeholders})"
    cursor.executemany(sql, rows)


def _hydrate_video_metrics(cursor):
    """Promote phase-event timings into render_runs for dashboard consumers.

    Some CLI builds write the detailed timings only into render_phase_events
    while leaving the denormalized render_runs columns at zero. The dashboard
    expects those top-level fields to be populated, so we fill them from the
    phase table when the cached merged DB is built.
    """

    phase_to_column = {
        'chronon_render_only_ms': 'chronon_render_only_ms',
        'chronon_conversion_copy_ms': 'chronon_conversion_copy_ms',
        'chronon_queue_wait_ms': 'chronon_queue_wait_ms',
        'ffmpeg_encode_total_ms': 'ffmpeg_encode_total_ms',
        'ffmpeg_flush_close_ms': 'ffmpeg_flush_close_ms',
        'e2e_wall_ms': 'e2e_wall_ms',
    }

    for phase_name, column_name in phase_to_column.items():
        cursor.execute(
            f"""
            UPDATE render_runs
               SET {column_name} = COALESCE(
                   NULLIF({column_name}, 0),
                   (
                       SELECT duration_ms
                         FROM render_phase_events
                        WHERE render_phase_events.run_id = render_runs.run_id
                          AND phase_name = ?
                        LIMIT 1
                   ),
                   {column_name}
               )
             WHERE ({column_name} IS NULL OR {column_name} = 0)
               AND render_runs.run_id NOT LIKE 'legacy\_%' ESCAPE '\\'
            """,
            (phase_name,),
        )

    cursor.execute(
        """
        UPDATE render_runs
           SET chronon_render_throughput_ms =
               COALESCE(chronon_render_only_ms, 0) +
               COALESCE(chronon_conversion_copy_ms, 0) +
               COALESCE(chronon_queue_wait_ms, 0)
         WHERE run_id NOT LIKE 'legacy\_%' ESCAPE '\\'
        """
    )


# ── Global cache: one shared in-memory master DB, rebuilt only on file change ────
_cached_master = None
_last_db_mtime = 0
_last_jsonl_mtime = 0
_db_lock = threading.Lock()


def create_merged_connection():
    """
    Create a read-only connection to the merged telemetry data.

    Returns a SharedReadConnection that wraps the cached in-memory database.
    Unlike the previous approach that used sqlite3.backup() to copy the entire
    cached DB (potentially 1+ GB) for every HTTP request, this shares the
    single in-memory connection using a lock for serialized reads.

    The cached master database is rebuilt only when the on-disk SQLite DB or
    JSONL file has been modified since the last rebuild.
    """
    global _cached_master, _last_db_mtime, _last_jsonl_mtime

    with _db_lock:
        db_mtime = DB_PATH.stat().st_mtime if DB_PATH.exists() else 0
        jsonl_mtime = JSONL_PATH.stat().st_mtime if JSONL_PATH.exists() else 0

        if _cached_master is None or db_mtime > _last_db_mtime or jsonl_mtime > _last_jsonl_mtime:
            schema_sql = _load_schema_sql()
            master = sqlite3.connect(':memory:', check_same_thread=False)
            master.row_factory = sqlite3.Row
            cursor = master.cursor()
            cursor.executescript(schema_sql)
            _load_existing_db(cursor)

            batches = {rtype: [] for rtype in _BUILDERS}
            for record in load_jsonl_records():
                rtype = record.get('type')
                builder = _BUILDERS.get(rtype)
                if builder:
                    batches[rtype].append(builder[0](record))

            for rtype, rows in batches.items():
                if rows:
                    _, columns, table = _BUILDERS[rtype]
                    _insert_batch(cursor, rows, columns, table)

            _hydrate_video_metrics(cursor)
            master.commit()

            if _cached_master:
                try:
                    _cached_master.close()
                except Exception:
                    pass
            _cached_master = master
            _last_db_mtime = db_mtime
            _last_jsonl_mtime = jsonl_mtime

    # Return a shared read wrapper — no backup() copy needed
    return SharedReadConnection(_cached_master, _db_lock)
