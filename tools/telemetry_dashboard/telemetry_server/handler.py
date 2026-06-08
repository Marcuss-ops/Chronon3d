import json
import os
import urllib.parse
from pathlib import Path
from http.server import BaseHTTPRequestHandler

from .config import DB_PATH, ARTIFACT_MIME_TYPES, STATIC_MIME_TYPES
from .database import create_merged_connection


def resolve_artifact_path(raw_path: str) -> Path | None:
    path = Path(raw_path)
    if path.is_absolute():
        candidates = [path]
    else:
        project_root = Path(__file__).resolve().parent.parent.parent.parent
        candidates = [
            Path.cwd() / path,
            project_root / path,
            project_root / 'build' / 'chronon' / 'linux-release' / path,
            project_root / 'build' / path,
        ]

    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except Exception:
            continue
        if resolved.exists() and resolved.is_file():
            return resolved

    if not path.is_absolute():
        for parent in [Path.cwd(), project_root, project_root / 'build']:
            if not parent.exists():
                continue
            try:
                match = next(parent.rglob(path.name))
            except StopIteration:
                continue
            if match.exists() and match.is_file():
                return match.resolve()

    return None


class TelemetryAPIHandler(BaseHTTPRequestHandler):
    """HTTP request handler for the Chronon3D telemetry dashboard API + static files."""

    # ── CORS ──────────────────────────────────────────────────────────────────────
    def send_cors_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_cors_headers()
        self.end_headers()

    # ── JSON response helper ──────────────────────────────────────────────────────
    def send_json(self, data, status=200):
        try:
            body = json.dumps(data).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_cors_headers()
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except Exception as e:
            print(f"Error encoding JSON response: {e}")

    # ── Routing ───────────────────────────────────────────────────────────────────
    def do_GET(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path

        if path == '/api/runs':
            self.handle_get_runs()
        elif path.startswith('/api/run/'):
            run_id = path[len('/api/run/'):]
            self.handle_get_run_detail(run_id)
        elif path == '/artifact':
            self.handle_get_artifact(parsed_url.query)
        else:
            self.handle_static_file(path)

    def do_POST(self):
        self.send_response(404)
        self.end_headers()

    # ── API: GET /api/runs ────────────────────────────────────────────────────────
    def handle_get_runs(self):
        conn = None
        try:
            conn = create_merged_connection()
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM render_runs ORDER BY finished_at_iso DESC")
            runs = [dict(row) for row in cursor.fetchall()]
            self.send_json(runs)
        except Exception as e:
            self.send_json({"error": str(e)}, 500)
        finally:
            try:
                if conn:
                    conn.close()
            except Exception:
                pass

    # ── API: GET /api/run/<run_id> ─────────────────────────────────────────────────
    def handle_get_run_detail(self, run_id):
        conn = None
        try:
            conn = create_merged_connection()
            cursor = conn.cursor()

            cursor.execute("SELECT * FROM render_runs WHERE run_id = ?", (run_id,))
            run_row = cursor.fetchone()
            if not run_row:
                self.send_json({"error": "Run not found"}, 404)
                return

            run_detail = dict(run_row)
            cursor.execute(
                "SELECT * FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC",
                (run_id,),
            )
            frames = [dict(row) for row in cursor.fetchall()]
            cursor.execute(
                "SELECT * FROM render_phase_events WHERE run_id = ? ORDER BY duration_ms DESC",
                (run_id,),
            )
            phases = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_counters WHERE run_id = ?", (run_id,))
            counters = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_node_events WHERE run_id = ?", (run_id,))
            node_events = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_layer_events WHERE run_id = ?", (run_id,))
            layer_events = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_cache_events WHERE run_id = ?", (run_id,))
            cache_events = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_culling_events WHERE run_id = ?", (run_id,))
            culling_events = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_text_events WHERE run_id = ?", (run_id,))
            text_events = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_image_events WHERE run_id = ?", (run_id,))
            image_events = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_tile_events WHERE run_id = ?", (run_id,))
            tile_events = [dict(row) for row in cursor.fetchall()]

            self.send_json({
                "run": run_detail,
                "frames": frames,
                "phases": phases,
                "counters": counters,
                "node_events": node_events,
                "layer_events": layer_events,
                "cache_events": cache_events,
                "culling_events": culling_events,
                "text_events": text_events,
                "image_events": image_events,
                "tile_events": tile_events,
            })
        except Exception as e:
            self.send_json({"error": str(e)}, 500)
        finally:
            try:
                if conn:
                    conn.close()
            except Exception:
                pass

    # ── API: GET /artifact?path=... ────────────────────────────────────────────────
    def handle_get_artifact(self, query_string):
        params = urllib.parse.parse_qs(query_string)
        raw_path = params.get('path', [''])[0]
        if not raw_path:
            self.send_response(400)
            self.end_headers()
            return

        artifact_path = resolve_artifact_path(raw_path)
        if artifact_path is None:
            self.send_response(404)
            self.end_headers()
            return

        content_type = ARTIFACT_MIME_TYPES.get(
            artifact_path.suffix.lower(), 'application/octet-stream'
        )

        try:
            with open(artifact_path, 'rb') as f:
                content = f.read()
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(content)))
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(content)
        except Exception:
            self.send_response(404)
            self.end_headers()

    # ── Static file serving (React SPA) ────────────────────────────────────────────
    def handle_static_file(self, path):
        base_dir = os.path.join(os.path.dirname(__file__), '..', 'frontend', 'dist')
        base_dir = os.path.abspath(base_dir)

        if path == '/' or not path:
            path = '/index.html'

        file_path = os.path.abspath(os.path.join(base_dir, path.lstrip('/')))
        # Security check: ensure path is within base_dir
        if not file_path.startswith(base_dir):
            self.send_response(403)
            self.end_headers()
            return

        if not os.path.exists(file_path) or os.path.isdir(file_path):
            # SPA Fallback: serve index.html for unknown routes
            file_path = os.path.join(base_dir, 'index.html')

        _, ext = os.path.splitext(file_path)
        content_type = STATIC_MIME_TYPES.get(ext.lower(), 'application/octet-stream')

        try:
            with open(file_path, 'rb') as f:
                content = f.read()
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(content)))
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(content)
        except Exception as e:
            self.send_response(404)
            self.end_headers()
