#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Chronon3D dashboard backend (Flask, single source file).
#
# ENDPOINTS
#   GET  /api/health         health JSON
#   GET  /api/runs           list of run summaries (newest first) from JSONL
#   GET  /api/run/<run_id>   per-run detail (run + frames + phases + counters
#                           + node_events + layer_events + cache_events)
#                           Top-level fields are REAL.  frames/phases/counters
#                           etc. are SYNTHESIZED deterministically from the
#                           run-level numbers.
#   GET  /api/graph/<comp>   static 7-node logical render graph for ReactFlow
#   GET  /artifact?path=...  serve a file under PROJECT_ROOT (path traversal
#                           blocked)
#   *    /socket.io/*        minimal Engine.IO handshake stub (frontend falls
#                           back to /api/runs polling)

import json
import logging
import mimetypes
import os
import threading
from datetime import datetime, timezone
from http import HTTPStatus
from pathlib import Path

from flask import Flask, abort, jsonify, request, send_file
from flask_cors import CORS

# ── Config ──────────────────────────────────────────────────────────────
LOG = logging.getLogger("chronon3d_dashboard")
logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(name)s: %(message)s")

DEFAULT_PROJECT_ROOT = "/home/pierone/src/go-master/projects/Pyt/Chrono3d"
DEFAULT_HISTORY_PATH = "~/.chronon3d/telemetry/render_history.jsonl"

PROJECT_ROOT = Path(os.environ.get("CHRONON3D_PROJECT_ROOT", DEFAULT_PROJECT_ROOT)).resolve()
HISTORY_PATH = Path(os.environ.get("CHRONON3D_HISTORY_PATH", DEFAULT_HISTORY_PATH)).expanduser().resolve()
PORT = int(os.environ.get("CHRONON3D_BACKEND_PORT", "8000"))

MAX_SYNTH_ITEMS = 4096  # cap on synthesised list sizes (DoS backstop)

LOG.info("PROJECT_ROOT = %s", PROJECT_ROOT)
LOG.info("HISTORY_PATH = %s", HISTORY_PATH)
LOG.info("PORT         = %d", PORT)

# ── Flask app + CORS ────────────────────────────────────────────────────
app = Flask(__name__)
CORS(app)

# ── JSONL cache (mtime-invalidated, thread-safe) ────────────────────────
_CACHE_LOCK = threading.Lock()
_CACHE = {"mtime": None, "runs": [], "by_id": {}}


def _safe_load_history():
    runs = []
    try:
        with open(HISTORY_PATH, "r", encoding="utf-8") as f:
            for n, line in enumerate(f, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError as exc:
                    LOG.warning("Skipping malformed JSONL line %d: %s", n, exc)
                    continue
                runs.append(_normalise_run(rec))
    except FileNotFoundError:
        LOG.info("render_history.jsonl not found at %s (empty cache)", HISTORY_PATH)
    except OSError as exc:
        LOG.warning("Could not read %s: %s", HISTORY_PATH, exc)
    runs.sort(
        key=lambda r: (r.get("finished_at_iso") or r.get("started_at_iso") or ""),
        reverse=True,
    )
    return runs


def _normalise_run(rec):
    out = dict(rec)
    out.setdefault("type", "render")
    out.setdefault("success", True)
    out.setdefault("dirty_pixels", 0)
    out.setdefault("bytes_allocated_peak", 0)
    out.setdefault("dirty_full_fallbacks", 0)
    out.setdefault("frames_total", 1)
    out.setdefault("frames_written", 1)
    out.setdefault("cache_hits", 0)
    out.setdefault("cache_misses", 0)
    out.setdefault("pixels_touched", 0)
    out.setdefault("framebuffer_allocations", 0)
    out.setdefault("framebuffer_reuses", 0)
    out.setdefault("framebuffer_bytes_allocated", 0)
    out.setdefault("framebuffer_bytes_peak", 0)
    out.setdefault("effective_fps", 0.0)
    out.setdefault("wall_time_ms", 0.0)
    out.setdefault("render_ms", 0.0)
    out.setdefault("encode_ms", 0.0)
    out.setdefault("os", "linux")
    out.setdefault("cpu_model", "unknown")
    out.setdefault("cores", 1)
    out.setdefault("build_type", "Release")
    out.setdefault("compiler_info", "unknown")
    out.setdefault("git_commit_short", "unknown")
    out.setdefault("output_path", "")
    return out


def _refresh_cache():
    try:
        mtime = HISTORY_PATH.stat().st_mtime
    except FileNotFoundError:
        with _CACHE_LOCK:
            _CACHE["mtime"] = None
            _CACHE["runs"] = []
            _CACHE["by_id"] = {}
        return
    with _CACHE_LOCK:
        if _CACHE["mtime"] == mtime:
            return
        LOG.info("Reloading render_history.jsonl (mtime=%.0f)", mtime)
        runs = _safe_load_history()
        _CACHE["runs"] = runs
        _CACHE["by_id"] = {r["run_id"]: r for r in runs if r.get("run_id")}
        _CACHE["mtime"] = mtime


def _runs():
    _refresh_cache()
    with _CACHE_LOCK:
        return list(_CACHE["runs"])


def _by_id(run_id):
    _refresh_cache()
    with _CACHE_LOCK:
        return _CACHE["by_id"].get(run_id)


# ── Synthesis helpers (deterministic, run-level → per-frame/counters) ───
def _seeded_jitter(seed_str, frame_idx):
    """Stable pseudo-random per (run, frame) so re-polls don't shift charts."""
    h = hash((seed_str, frame_idx))
    return ((h % 10000) / 10000.0) * 2.0 - 1.0


def _synth_frames(run):
    """Expand run-level render_ms into per-frame records.  Capped at MAX_SYNTH_ITEMS."""
    raw_frames = int(run.get("frames_total") or 1)
    frames_total = max(1, min(raw_frames, MAX_SYNTH_ITEMS))
    if raw_frames > MAX_SYNTH_ITEMS:
        LOG.warning("synth cap hit: frames_total=%d -> %d for run %s",
                    raw_frames, frames_total, run.get("run_id"))
    render_ms = float(run.get("render_ms") or 0.0)
    avg_ms = render_ms / frames_total if frames_total else 0.0
    seed = f"{run.get('run_id', '')}/{run.get('composition_id', '')}"
    cache_hits = int(run.get("cache_hits") or 0)
    cache_misses = int(run.get("cache_misses") or 0)
    cache_total = cache_hits + cache_misses
    frames = []
    for i in range(frames_total):
        jitter = _seeded_jitter(seed, i) * 0.35
        duration_ms = max(0.0, avg_ms * (1.0 + jitter))
        cache_hit = cache_total > 0 and (i % 2 == 0)
        dirty_area_ratio = max(0.0, min(1.0, 0.45 + _seeded_jitter(seed, i + 7) * 0.15))
        frames.append({
            "frame_number": i,
            "duration_ms": duration_ms,
            "cache_hit": cache_hit,
            "dirty_area_ratio": dirty_area_ratio,
            "fast_path_reused": 1 if cache_hit else 0,
        })
    return frames


def _synth_phases(run):
    """Per-phase duration breakdown.  Empirical software-renderer splits."""
    render_ms = float(run.get("render_ms") or 0.0)
    if render_ms <= 0.0:
        return []
    splits = [
        ("rendering_loop",  0.34),
        ("setup_renderer",  0.07),
        ("Composite",       0.21),
        ("Clear",           0.06),
        ("Transform",       0.10),
        ("EffectStack",     0.12),
        ("bg",              0.04),
        ("lbl",             0.06),
    ]
    return [{"phase_name": name, "duration_ms": render_ms * frac}
            for name, frac in splits]


def _synth_counters(run):
    """Counters keyed by name App.jsx reads via c('counter_name')."""
    render_ms = float(run.get("render_ms") or 0.0)
    wall_ms = float(run.get("wall_time_ms") or 0.0)
    reuses = int(run.get("framebuffer_reuses") or 0)
    cores = int(run.get("cores") or 1)
    counters = [
        ("framebuffer_pool_exact_hit",               reuses),
        ("framebuffer_pool_empty_alloc",             0),
        ("framebuffer_pool_best_fit_reuse",          0),
        ("framebuffer_buffer_returned_to_pool_count", reuses),
        ("framebuffer_acquire_ms",                   0),
        ("framebuffer_clear_ms",                     0),
        ("framebuffer_enqueue_ms",                   0),
        ("frame_conversion_copy_ms",                 0),
        ("dirty_full_fallback_predicted_bounds_missing", 0),
        ("dirty_full_fallback_composite_missing_input_bounds", 0),
        ("dirty_full_fallback_transform_bounds_unknown", 0),
        ("dirty_full_fallback_effect_bounds_unknown", 0),
        ("node_dispatch_ms",                         render_ms),
        ("node_execute_actual_ms",                   render_ms * 0.85),
        ("io_queue_push_blocked_ms",                 0),
        ("io_queue_pop_wait_ms",                     0),
        ("io_writer_idle_wait_ms",                   0),
        ("io_queue_peak_depth",                      1),
        ("process_cpu_user_ms",                      wall_ms * 0.7),
        ("process_cpu_sys_ms",                       wall_ms * 0.05),
        ("os_page_faults_minor",                     0),
        ("system_logical_cores",                     cores),
    ]
    return [{"counter_name": k, "counter_value": v} for k, v in counters]


def _synth_node_events(run):
    paint_ms = float(run.get("render_ms") or 0.0) * 0.6
    px = int(run.get("pixels_touched") or 0)
    return [
        {"node_name": "Clear",       "node_type": "Op",    "executions": 1,
         "duration_ms": paint_ms * 0.05, "hit_rate": 0.0, "pixels_touched": px},
        {"node_name": "Background",  "node_type": "Layer", "executions": 1,
         "duration_ms": paint_ms * 0.10, "hit_rate": 1.0, "pixels_touched": px},
        {"node_name": "TextRun",     "node_type": "Shape", "executions": 1,
         "duration_ms": paint_ms * 0.35, "hit_rate": 0.30, "pixels_touched": max(1, px // 4)},
        {"node_name": "Composite",   "node_type": "Op",    "executions": 1,
         "duration_ms": paint_ms * 0.25, "hit_rate": 0.0, "pixels_touched": px},
        {"node_name": "EffectStack", "node_type": "Op",    "executions": 1,
         "duration_ms": paint_ms * 0.15, "hit_rate": 0.50, "pixels_touched": px},
    ]


def _synth_layer_events(run):
    return [{
        "layer_name": run.get("composition_id", "Layer"),
        "layer_type": "CompositedLayer",
        "executions": 1,
        "duration_ms": float(run.get("render_ms") or 0.0),
        "visible_pixels":     int(run.get("pixels_touched") or 0),
        "dirty_pixels":       int(run.get("dirty_pixels")   or 0),
        "glyphs_rasterized":  0,
        "images_sampled":     0,
    }]


def _synth_cache_events(run):
    """Per-cache-event list.  Hits + misses, each capped at MAX_SYNTH_ITEMS
    so a hostile JSONL record with both fields == 10^6 produces at most
    2*MAX_SYNTH_ITEMS events."""
    raw_hits = int(run.get("cache_hits") or 0)
    raw_misses = int(run.get("cache_misses") or 0)
    if raw_hits + raw_misses > 2 * MAX_SYNTH_ITEMS:
        LOG.warning("synth cap hit: cache_events total=%d for run %s",
                    raw_hits + raw_misses, run.get("run_id"))
    cap = MAX_SYNTH_ITEMS
    events = [{"cache_status": "hit"} for _ in range(min(raw_hits,   cap))]
    events.extend({"cache_status": "miss"} for _ in range(min(raw_misses, cap)))
    return events


def _synth_graph(comp_id):
    return {
        "composition_id": comp_id,
        "nodes": [
            {"id": "input",   "label": "Input",       "color": "#94a3b8"},
            {"id": "resolve", "label": "Resolve",     "color": "#a78bfa"},
            {"id": "shape",   "label": "Shape",       "color": "#f472b6"},
            {"id": "raster",  "label": "Raster",      "color": "#fb923c"},
            {"id": "compose", "label": "Composite",   "color": "#34d399"},
            {"id": "effect",  "label": "EffectStack", "color": "#22d3ee"},
            {"id": "output",  "label": "Output",      "color": "#60a5fa"},
        ],
        "edges": [
            {"source": "input",   "target": "resolve"},
            {"source": "resolve", "target": "shape"},
            {"source": "shape",   "target": "raster"},
            {"source": "raster",  "target": "compose"},
            {"source": "compose", "target": "effect"},
            {"source": "effect",  "target": "output"},
        ],
    }


# ── Routes ──────────────────────────────────────────────────────────────
@app.get("/")
def root():
    return jsonify({
        "service": "chronon3d-dashboard-backend",
        "version": "0.1.0",
        "endpoints": [
            "/api/health",
            "/api/runs",
            "/api/run/<run_id>",
            "/api/graph/<composition_id>",
            "/artifact?path=<relative-path>",
            "/socket.io/",
        ],
        "project_root": str(PROJECT_ROOT),
        "history_path": str(HISTORY_PATH),
        "history_runs": len(_runs()),
    }), HTTPStatus.OK


@app.get("/api/health")
def api_health():
    runs = _runs()
    return jsonify({
        "status": "ok",
        "history_path": str(HISTORY_PATH),
        "history_exists": HISTORY_PATH.exists(),
        "history_runs": len(runs),
        "project_root": str(PROJECT_ROOT),
        "server_time_iso": datetime.now(timezone.utc).isoformat(),
    }), HTTPStatus.OK


@app.get("/api/runs")
def api_runs():
    """List of run summaries, newest first.  Matches App.jsx fetchRuns()."""
    return jsonify(_runs()), HTTPStatus.OK


@app.get("/api/run/<run_id>")
def api_run_detail(run_id):
    """Per-run detail.  Top-level fields REAL; rest SYNTHESIZED deterministically."""
    run = _by_id(run_id)
    if run is None:
        return jsonify({"error": f"run_id '{run_id}' not found"}), HTTPStatus.NOT_FOUND
    return jsonify({
        "_synthesis": {
            "frames":       True,
            "phases":       True,
            "counters":     True,
            "node_events":  True,
            "layer_events": True,
            "cache_events": True,
            "top_level":    False,
            "note":         "frames/phases/counters are deterministic synthesis "
                            "from real top-level JSONL fields; only `run` is real.",
        },
        "run":          run,
        "frames":       _synth_frames(run),
        "phases":       _synth_phases(run),
        "counters":     _synth_counters(run),
        "node_events":  _synth_node_events(run),
        "layer_events": _synth_layer_events(run),
        "cache_events": _synth_cache_events(run),
    }), HTTPStatus.OK


@app.get("/api/graph/<comp_id>")
def api_graph(comp_id):
    return jsonify(_synth_graph(comp_id)), HTTPStatus.OK


@app.get("/artifact")
def artifact():
    """Serve a file under PROJECT_ROOT, blocking `..` and out-of-tree symlinks."""
    rel_path = request.args.get("path", "").strip()
    if not rel_path:
        abort(HTTPStatus.BAD_REQUEST, description="missing 'path' query param")
    # Refuse obvious absolute-path injection before resolve() (defence in depth).
    if Path(rel_path).is_absolute():
        abort(HTTPStatus.FORBIDDEN, description="absolute paths are not allowed")
    candidate = (PROJECT_ROOT / rel_path).resolve()
    try:
        candidate.relative_to(PROJECT_ROOT)
    except ValueError:
        abort(HTTPStatus.FORBIDDEN, description="path escapes project root")
    if not candidate.exists():
        LOG.info("artifact miss: %s", candidate)
        abort(HTTPStatus.NOT_FOUND, description=f"no such file: {rel_path}")
    if candidate.is_dir():
        abort(HTTPStatus.BAD_REQUEST, description="directory not servable")
    mime, _ = mimetypes.guess_type(candidate.name)
    return send_file(candidate, mimetype=mime or "application/octet-stream",
                     conditional=True, max_age=0)


# /socket.io/* — minimal Engine.IO polling handshake stub so the Vite proxy
# stays healthy while the frontend's socket.io-client falls back onto its
# built-in /api/runs polling (App.jsx polls every 3s).
# Flask URL routing requires STRING rules (not compiled-regex patterns), so
# we register the bare /socket.io/ and the /socket.io/<path:subpath>
# catch-all as two routes on the same view function.
@app.get("/socket.io/")
@app.get("/socket.io/<path:subpath>")
@app.post("/socket.io/")
@app.post("/socket.io/<path:subpath>")
def socket_io_stub(subpath=""):
    eio = request.args.get("EIO", "4")
    transport = request.args.get("transport", "polling")
    if transport == "polling" and eio in {"3", "4"}:
        return ('0{"sid":"stub","upgrades":[],"pingInterval":25000,"pingTimeout":20000}\n'
                '40\n'), HTTPStatus.OK, {"Content-Type": "text/plain; charset=UTF-8"}
    return jsonify({"stub": True,
                    "note": "socket.io disabled; frontend polls /api/runs"}), HTTPStatus.OK


# ── Uniform JSON error handlers ─────────────────────────────────────────
def _err(exc):
    return jsonify({"error": str(getattr(exc, "description", exc))})


@app.errorhandler(HTTPStatus.NOT_FOUND)
def _not_found(exc):       return _err(exc), HTTPStatus.NOT_FOUND

@app.errorhandler(HTTPStatus.FORBIDDEN)
def _forbidden(exc):       return _err(exc), HTTPStatus.FORBIDDEN

@app.errorhandler(HTTPStatus.BAD_REQUEST)
def _bad_request(exc):     return _err(exc), HTTPStatus.BAD_REQUEST


@app.errorhandler(Exception)
def _generic(exc):
    # Full exception body logged server-side; client sees a stable string so
    # internal paths / class names cannot leak through the API (CWE-209).
    LOG.exception("Unhandled exception: %s", exc)
    return jsonify({"error": "internal server error"}), HTTPStatus.INTERNAL_SERVER_ERROR


# ── Entrypoint ──────────────────────────────────────────────────────────
if __name__ == "__main__":
    _refresh_cache()  # warm cache so first request is fast
    app.run(host="127.0.0.1", port=PORT, threaded=True, use_reloader=False)
