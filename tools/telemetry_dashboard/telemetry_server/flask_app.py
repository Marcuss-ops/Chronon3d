import os
import json
import sqlite3
import time
import secrets
from functools import wraps
from pathlib import Path
from flask import Flask, jsonify, request, send_file, Response
from flask_cors import CORS
from flask_socketio import SocketIO, emit
from .config import DB_PATH, JSONL_PATH, ARTIFACT_MIME_TYPES, STATIC_MIME_TYPES
from .database import create_merged_connection

app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*")

# ── Simple Password Auth ─────────────────────────────────────────────────────────────
DASHBOARD_PASSWORD = os.environ.get("CHRONON3D_DASHBOARD_PASSWORD", "")
# In-memory token store (valid for session lifetime)
auth_tokens = set()

def require_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        token = request.headers.get('Authorization', '').replace('Bearer ', '')
        if not token or token not in auth_tokens:
            return jsonify({"error": "Unauthorized"}), 401
        return f(*args, **kwargs)
    return decorated

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent.parent
OUTPUT_DIR = PROJECT_ROOT / 'output'
ALLOWED_ARTIFACT_ROOTS = [OUTPUT_DIR]


def resolve_artifact_path(raw_path: str) -> Path | None:
    """Resolve artifact paths safely, restricted to allowed artifact directories.

    Only relative paths within ALLOWED_ARTIFACT_ROOTS are accepted.
    Absolute paths and path-traversal attempts are rejected.
    """
    path = Path(raw_path)

    # Reject absolute paths outright
    if path.is_absolute():
        return None

    for root in ALLOWED_ARTIFACT_ROOTS:
        resolved_root = root.resolve()
        candidate = resolved_root / path
        try:
            resolved = candidate.resolve()
        except Exception:
            continue
        if resolved.exists() and resolved.is_file() and resolved.is_relative_to(resolved_root):
            return resolved

    return None

@app.route('/api/login', methods=['POST'])
def login():
    if not DASHBOARD_PASSWORD:
        return jsonify({"error": "Dashboard password not configured (set CHRONON3D_DASHBOARD_PASSWORD)"}), 500
    data = request.get_json() or {}
    password = data.get('password', '')
    if password == DASHBOARD_PASSWORD:
        token = secrets.token_urlsafe(32)
        auth_tokens.add(token)
        return jsonify({"token": token, "success": True})
    return jsonify({"error": "Invalid password"}), 403

@app.route('/api/logout', methods=['POST'])
def logout():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    auth_tokens.discard(token)
    return jsonify({"success": True})

@app.route('/api/runs')
@require_auth
def get_runs():
    conn = None
    try:
        conn = create_merged_connection()
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM render_runs ORDER BY finished_at_iso DESC")
        runs = [dict(row) for row in cursor.fetchall()]
        return jsonify(runs)
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        if conn:
            conn.close()

@app.route('/api/run/<run_id>')
@require_auth
def get_run_detail(run_id):
    conn = None
    try:
        conn = create_merged_connection()
        cursor = conn.cursor()

        cursor.execute("SELECT * FROM render_runs WHERE run_id = ?", (run_id,))
        run_row = cursor.fetchone()
        if not run_row:
            return jsonify({"error": "Run not found"}), 404

        run_detail = dict(run_row)
        
        # Helper to get all data for a run
        def fetch_table(table, order_by=None):
            allowed_tables = {
                "render_frames", "render_phase_events", "render_counters",
                "render_node_events", "render_layer_events", "render_cache_events",
                "render_culling_events", "render_text_events", "render_image_events",
                "render_tile_events"
            }
            if table not in allowed_tables:
                return []
            sql = f"SELECT * FROM {table} WHERE run_id = ?"
            if order_by:
                sql += f" ORDER BY {order_by}"
            cursor.execute(sql, (run_id,))
            return [dict(row) for row in cursor.fetchall()]

        return jsonify({
            "run": run_detail,
            "frames": fetch_table("render_frames", "frame_number ASC"),
            "phases": fetch_table("render_phase_events", "duration_ms DESC"),
            "counters": fetch_table("render_counters"),
            "node_events": fetch_table("render_node_events"),
            "layer_events": fetch_table("render_layer_events"),
            "cache_events": fetch_table("render_cache_events"),
            "culling_events": fetch_table("render_culling_events"),
            "text_events": fetch_table("render_text_events"),
            "image_events": fetch_table("render_image_events"),
            "tile_events": fetch_table("render_tile_events"),
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        if conn:
            conn.close()

@app.route('/artifact')
@require_auth
def get_artifact():
    raw_path = request.args.get('path', '')
    if not raw_path:
        return "Missing path", 400

    artifact_path = resolve_artifact_path(raw_path)
    if artifact_path is None:
        return "Not found", 404

    content_type = ARTIFACT_MIME_TYPES.get(artifact_path.suffix.lower(), 'application/octet-stream')
    return send_file(artifact_path, mimetype=content_type)


@app.route('/output')
@require_auth
def output_gallery():
    """Gallery page showing all rendered output PNGs."""
    if not OUTPUT_DIR.exists():
        return "<h1>No output directory</h1>", 404

    pngs = sorted(OUTPUT_DIR.glob('*.png'), key=os.path.getmtime, reverse=True)
    if not pngs:
        return "<h1>No rendered images yet</h1><p>Run a composition first: <code>./chronon3d_cli render YourComposition -o output/your.png</code></p>"

    cards = ''
    for p in pngs:
        fname = p.name
        size_kb = p.stat().st_size // 1024
        cards += f'''
        <div class="card">
          <img src="/output/{fname}" loading="lazy" onclick="openModal(this.src)">
          <div class="info">
            <span class="name">{fname}</span>
            <span class="size">{size_kb} KB</span>
          </div>
        </div>'''

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Chronon3D — Render Outputs</title>
<style>
* {{ margin:0; padding:0; box-sizing:border-box; }}
body {{ font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif; background:#0d1117; color:#c9d1d9; padding:20px; }}
h1 {{ color:#58a6ff; margin-bottom:6px; }}
.subtitle {{ color:#8b949e; font-size:14px; margin-bottom:20px; }}
.gallery {{ display:grid; grid-template-columns:repeat(auto-fill,minmax(400px,1fr)); gap:16px; }}
.card {{ background:#161b22; border:1px solid #30363d; border-radius:8px; overflow:hidden; transition:border-color .2s; }}
.card:hover {{ border-color:#58a6ff; }}
.card img {{ width:100%; display:block; cursor:pointer; }}
.card .info {{ padding:10px 14px; display:flex; justify-content:space-between; align-items:center; }}
.card .name {{ font-size:13px; font-weight:600; }}
.card .size {{ font-size:11px; color:#8b949e; }}

/* Modal */
.modal {{ display:none; position:fixed; top:0; left:0; width:100%; height:100%; background:rgba(0,0,0,.9); z-index:1000; cursor:pointer; }}
.modal img {{ max-width:95vw; max-height:95vh; position:absolute; top:50%; left:50%; transform:translate(-50%,-50%); }}
.modal .close {{ position:absolute; top:16px; right:24px; font-size:36px; color:#fff; cursor:pointer; }}
</style>
</head>
<body>
<h1>🖼️ Chronon3D Render Outputs</h1>
<p class="subtitle">{len(pngs)} file PNG</p>
<div class="gallery">
{cards}
</div>

<div class="modal" id="modal" onclick="closeModal()">
  <span class="close">&times;</span>
  <img id="modalImg">
</div>

<script>
function openModal(src) {{
  document.getElementById('modal').style.display = 'block';
  document.getElementById('modalImg').src = src;
}}
function closeModal() {{
  document.getElementById('modal').style.display = 'none';
}}
document.addEventListener('keydown', function(e) {{ if (e.key === 'Escape') closeModal(); }});
</script>
</body>
</html>'''
    return Response(html, mimetype='text/html')


@app.route('/output/<path:filename>')
@require_auth
def serve_output(filename):
    """Serve individual output file (PNG, MP4, etc.)."""
    filepath = OUTPUT_DIR / filename
    safe_path = filepath.resolve()
    # Prevent directory traversal
    if not str(safe_path).startswith(str(OUTPUT_DIR.resolve())):
        return "Forbidden", 403
    if not safe_path.exists() or not safe_path.is_file():
        return "Not found", 404
    content_type = ARTIFACT_MIME_TYPES.get(safe_path.suffix.lower(), 'application/octet-stream')
    return send_file(safe_path, mimetype=content_type)


@app.route('/', defaults={'path': ''})
@app.route('/<path:path>')
def serve_static(path):
    base_dir = os.path.join(os.path.dirname(__file__), '..', 'frontend', 'dist')
    if not path or path == '/':
        return send_file(os.path.join(base_dir, 'index.html'))
    
    file_path = os.path.join(base_dir, path)
    if os.path.exists(file_path) and not os.path.isdir(file_path):
        return send_file(file_path)
    else:
        return send_file(os.path.join(base_dir, 'index.html'))

import re

@app.route('/api/graph/<composition_id>')
@require_auth
def get_graph(composition_id):
    dot_path = PROJECT_ROOT / "output" / f"{composition_id}_graph.dot"
    cli_path = PROJECT_ROOT / "build_vs" / "apps" / "chronon3d_cli" / "Release" / "chronon3d_cli.exe"
    
    # Run CLI to generate DOT
    import subprocess
    try:
        subprocess.run([str(cli_path), "graph", composition_id, "--output", str(dot_path)], check=True, capture_output=True)
        if not dot_path.exists():
            return jsonify({"error": "Failed to generate graph"}), 500
        
        dot_content = dot_path.read_text()
        
        # Simple DOT parser
        nodes = []
        edges = []
        
        # Parse nodes: n0 [label="Clear", fillcolor="orange"];
        node_matches = re.finditer(r'(n\d+)\s+\[label="([^"]+)",\s+fillcolor="([^"]+)"\];', dot_content)
        for m in node_matches:
            nodes.append({
                "id": m.group(1),
                "label": m.group(2),
                "color": m.group(3)
            })
            
        # Parse edges: n1 -> n3;
        edge_matches = re.finditer(r'(n\d+)\s+->\s+(n\d+);', dot_content)
        for m in edge_matches:
            edges.append({
                "source": m.group(1),
                "target": m.group(2)
            })
            
        return jsonify({"nodes": nodes, "edges": edges})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

def watch_database():
    """Background thread that watches the database for changes and emits events."""
    last_mtime = 0
    if DB_PATH.exists():
        last_mtime = max(last_mtime, os.path.getmtime(DB_PATH))
    if JSONL_PATH.exists():
        last_mtime = max(last_mtime, os.path.getmtime(JSONL_PATH))
    
    last_run_id = None
    preserve_generic_videos = os.environ.get("CHRONON3D_TELEMETRY_PRESERVE_VIDEOS", "").strip().lower() in {"1", "true", "yes", "on"}

    while True:
        try:
            current_mtime = 0
            if DB_PATH.exists():
                current_mtime = max(current_mtime, os.path.getmtime(DB_PATH))
            if JSONL_PATH.exists():
                current_mtime = max(current_mtime, os.path.getmtime(JSONL_PATH))

            if current_mtime > last_mtime:
                last_mtime = current_mtime

                # Check for the latest merged run, regardless of whether it landed in SQLite or JSONL.
                conn = create_merged_connection()
                cursor = conn.cursor()
                cursor.execute("SELECT * FROM render_runs ORDER BY finished_at_iso DESC LIMIT 1")
                row = cursor.fetchone()
                conn.close()

                if row:
                    current_run_id = row['run_id']
                    
                    # Optional unique video preservation, disabled by default.
                    output_path = row['output_path'] or ''
                    if preserve_generic_videos and output_path and (output_path.endswith('rendered_video.mp4') or output_path.endswith('rendered_video.webm') or output_path.endswith('rendered_video.mov')):
                        src_path = Path(output_path)
                        if src_path.exists() and src_path.is_file():
                            dest_name = f"run_{current_run_id}{src_path.suffix}"
                            dest_path = src_path.parent / dest_name
                            if not dest_path.exists():
                                import shutil
                                shutil.copy2(src_path, dest_path)
                                print(f"[telemetry_server] Auto-preserved generic video to unique path: {dest_path}")
                                
                                # Append updated run to JSONL so it persists permanently
                                run_dict = dict(row)
                                run_dict['type'] = 'run'
                                run_dict['output_path'] = str(dest_path)
                                with open(JSONL_PATH, 'a', encoding='utf-8') as f:
                                    f.write(json.dumps(run_dict) + '\n')
                                
                                # Trigger local mtime update so it re-merges immediately
                                if JSONL_PATH.exists():
                                    last_mtime = os.path.getmtime(JSONL_PATH)

                    if current_run_id != last_run_id:
                        last_run_id = current_run_id
                        socketio.emit('new_run', {'run_id': current_run_id})
            
            socketio.sleep(2)
        except Exception as e:
            print(f"Error in database watcher: {e}")
            socketio.sleep(5)

@socketio.on('connect')
def handle_connect():
    print('Client connected')

# Start the database watcher background task eagerly when imported
socketio.start_background_task(watch_database)

if __name__ == '__main__':
    socketio.run(app, host='127.0.0.1', port=8000, debug=True, allow_unsafe_werkzeug=True)
