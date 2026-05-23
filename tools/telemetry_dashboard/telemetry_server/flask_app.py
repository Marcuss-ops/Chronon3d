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
DASHBOARD_PASSWORD = "ciao"
# In-memory token store (valid for session lifetime)
auth_tokens = set()

def require_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        return f(*args, **kwargs)
    return decorated

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent.parent

@app.route('/api/login', methods=['POST'])
def login():
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
def get_artifact():
    # Bypassed auth check

    raw_path = request.args.get('path', '')
    if not raw_path:
        return "Missing path", 400

    artifact_path = Path(raw_path)
    if not artifact_path.is_absolute():
        artifact_path = PROJECT_ROOT / artifact_path
    artifact_path = artifact_path.resolve()

    if not artifact_path.exists() or artifact_path.is_dir():
        # Try relative to project root if absolute failed or is outside
        artifact_path = PROJECT_ROOT / Path(raw_path).name
        if not artifact_path.exists():
            return "Not found", 404

    content_type = ARTIFACT_MIME_TYPES.get(artifact_path.suffix.lower(), 'application/octet-stream')
    return send_file(artifact_path, mimetype=content_type)

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
                cursor.execute("SELECT run_id FROM render_runs ORDER BY finished_at_iso DESC LIMIT 1")
                row = cursor.fetchone()
                conn.close()

                if row:
                    current_run_id = row['run_id']
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
    socketio.run(app, host='0.0.0.0', port=8000, debug=True, allow_unsafe_werkzeug=True)
