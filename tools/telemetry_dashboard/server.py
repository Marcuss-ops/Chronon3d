import os
import sys
import json
import sqlite3
import urllib.parse
from pathlib import Path
from http.server import HTTPServer, BaseHTTPRequestHandler

TELEMETRY_DIR = Path(os.path.expanduser('~')) / '.chronon3d' / 'telemetry'
DB_PATH = TELEMETRY_DIR / 'chronon3d_render_history.sqlite'
JSONL_PATH = TELEMETRY_DIR / 'render_history.jsonl'

def parse_iso(value):
    if not value:
        return ''
    return str(value)

def load_jsonl_records():
    if not JSONL_PATH.exists():
        return []

    records = []
    with JSONL_PATH.open('r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except Exception:
                continue
            records.append(obj)
    return records

def jsonl_runs_by_id():
    runs = {}
    for record in load_jsonl_records():
        if record.get('type') != 'run':
            continue
        run_id = record.get('run_id')
        if not run_id:
            continue
        runs[run_id] = record
    return runs

def jsonl_run_detail(run_id):
    detail = None
    frames = []
    phases = []
    counters = []

    for record in load_jsonl_records():
        if record.get('run_id') != run_id:
            continue
        record_type = record.get('type')
        if record_type == 'run':
            detail = record
        elif record_type == 'frame':
            frames.append(record)
        elif record_type == 'phase':
            phases.append(record)
        elif record_type == 'counter':
            counters.append(record)

    if detail is None:
        return None

    frames.sort(key=lambda row: row.get('frame_number', 0))
    phases.sort(key=lambda row: row.get('duration_ms', 0.0), reverse=True)
    counters.sort(key=lambda row: row.get('counter_name', ''))

    return {
        'run': detail,
        'frames': frames,
        'phases': phases,
        'counters': counters,
    }

def create_merged_connection():
    conn = sqlite3.connect(':memory:')
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()

    cursor.executescript("""
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
    """)

    if DB_PATH.exists():
        source = sqlite3.connect(str(DB_PATH))
        source.row_factory = sqlite3.Row
        try:
            for table in ('render_runs', 'render_frames', 'render_phase_events', 'render_counters'):
                try:
                    rows = source.execute(f"SELECT * FROM {table}").fetchall()
                except Exception:
                    rows = []
                if not rows:
                    continue
                cols = rows[0].keys()
                placeholders = ', '.join(['?'] * len(cols))
                col_list = ', '.join(cols)
                sql = f"INSERT OR REPLACE INTO {table} ({col_list}) VALUES ({placeholders})"
                cursor.executemany(sql, [[row[col] for col in cols] for row in rows])
            conn.commit()
        finally:
            source.close()

    run_columns = [
        'run_id', 'composition_id', 'output_path', 'success', 'error_code', 'error_message',
        'frames_total', 'frames_written', 'wall_time_ms', 'render_ms', 'encode_ms',
        'effective_fps', 'pixels_touched', 'cache_hits', 'cache_misses', 'nodes_executed',
        'layers_rendered', 'text_glyphs_rasterized', 'images_sampled', 'blur_pixels',
        'simd_lerp_calls', 'tiles_total', 'tiles_hit', 'tiles_miss', 'tiles_partial',
        'bytes_allocated_peak', 'node_cache_hash_collisions', 'started_at_iso',
        'finished_at_iso', 'git_commit_short', 'build_type', 'compiler_info', 'os',
        'cpu_model', 'cores'
    ]

    run_rows = []
    frame_rows = []
    phase_rows = []
    counter_rows = []

    for record in load_jsonl_records():
        record_type = record.get('type')
        if record_type == 'run':
            run_rows.append([record.get(col) for col in run_columns])
        elif record_type == 'frame':
            frame_rows.append([
                record.get('run_id'),
                record.get('frame_number'),
                record.get('duration_ms'),
                int(bool(record.get('cache_hit'))),
                record.get('dirty_area_ratio'),
            ])
        elif record_type == 'phase':
            phase_rows.append([
                record.get('run_id'),
                record.get('phase_name'),
                record.get('duration_ms'),
            ])
        elif record_type == 'counter':
            counter_rows.append([
                record.get('run_id'),
                record.get('counter_name'),
                record.get('counter_value'),
            ])

    if run_rows:
        cursor.executemany(
            f"INSERT OR REPLACE INTO render_runs ({', '.join(run_columns)}) VALUES ({', '.join(['?'] * len(run_columns))})",
            run_rows
        )
    if frame_rows:
        cursor.executemany(
            "INSERT OR REPLACE INTO render_frames (run_id, frame_number, duration_ms, cache_hit, dirty_area_ratio) VALUES (?, ?, ?, ?, ?)",
            frame_rows
        )
    if phase_rows:
        cursor.executemany(
            "INSERT OR REPLACE INTO render_phase_events (run_id, phase_name, duration_ms) VALUES (?, ?, ?)",
            phase_rows
        )
    if counter_rows:
        cursor.executemany(
            "INSERT OR REPLACE INTO render_counters (run_id, counter_name, counter_value) VALUES (?, ?, ?)",
            counter_rows
        )

    conn.commit()
    return conn

class TelemetryAPIHandler(BaseHTTPRequestHandler):
    def send_cors_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_cors_headers()
        self.end_headers()

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
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path

        if path == '/api/query':
            self.handle_post_query()
        else:
            self.send_response(404)
            self.end_headers()

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
                conn.close()
            except Exception:
                pass

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
            cursor.execute("SELECT * FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC", (run_id,))
            frames = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_phase_events WHERE run_id = ? ORDER BY duration_ms DESC", (run_id,))
            phases = [dict(row) for row in cursor.fetchall()]
            cursor.execute("SELECT * FROM render_counters WHERE run_id = ?", (run_id,))
            counters = [dict(row) for row in cursor.fetchall()]

            self.send_json({
                "run": run_detail,
                "frames": frames,
                "phases": phases,
                "counters": counters
            })
        except Exception as e:
            self.send_json({"error": str(e)}, 500)
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def handle_get_artifact(self, query_string):
        params = urllib.parse.parse_qs(query_string)
        raw_path = params.get('path', [''])[0]
        if not raw_path:
            self.send_response(400)
            self.end_headers()
            return

        artifact_path = Path(raw_path)
        if not artifact_path.is_absolute():
            artifact_path = Path.cwd() / artifact_path
        artifact_path = artifact_path.resolve()

        if not artifact_path.exists() or artifact_path.is_dir():
            self.send_response(404)
            self.end_headers()
            return

        mime_types = {
            '.png': 'image/png',
            '.jpg': 'image/jpeg',
            '.jpeg': 'image/jpeg',
            '.webp': 'image/webp',
            '.gif': 'image/gif',
            '.svg': 'image/svg+xml',
            '.mp4': 'video/mp4',
            '.webm': 'video/webm',
            '.mov': 'video/quicktime'
        }
        content_type = mime_types.get(artifact_path.suffix.lower(), 'application/octet-stream')

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

    def handle_post_query(self):
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length)
        try:
            req_body = json.loads(post_data.decode('utf-8'))
            sql_query = req_body.get('query', '')
        except Exception as e:
            self.send_json({"error": "Invalid JSON body"}, 400)
            return

        if not sql_query:
            self.send_json({"error": "Query parameter is required"}, 400)
            return

        conn = None
        try:
            conn = create_merged_connection()
            cursor = conn.cursor()
            cursor.execute(sql_query)
            # Check if it returns rows
            if cursor.description:
                columns = [col[0] for col in cursor.description]
                rows = [dict(row) for row in cursor.fetchall()]
                self.send_json({"columns": columns, "rows": rows})
            else:
                conn.commit()
                self.send_json({"message": "Query executed successfully", "rows_affected": cursor.rowcount})
        except Exception as e:
            self.send_json({"error": str(e)}, 400)
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def handle_static_file(self, path):
        # Serve React frontend from frontend/dist
        base_dir = os.path.join(os.path.dirname(__file__), 'frontend', 'dist')
        if path == '/' or not path:
            path = '/index.html'

        file_path = os.path.abspath(os.path.join(base_dir, path.lstrip('/')))
        # Security check: ensure path is within base_dir
        if not file_path.startswith(os.path.abspath(base_dir)):
            self.send_response(403)
            self.end_headers()
            return

        if not os.path.exists(file_path) or os.path.isdir(file_path):
            # SPA Fallback: serve index.html for unknown routes
            file_path = os.path.join(base_dir, 'index.html')

        mime_types = {
            '.html': 'text/html',
            '.css': 'text/css',
            '.js': 'application/javascript',
            '.json': 'application/json',
            '.png': 'image/png',
            '.jpg': 'image/jpeg',
            '.svg': 'image/svg+xml',
            '.ico': 'image/x-icon'
        }
        _, ext = os.path.splitext(file_path)
        content_type = mime_types.get(ext.lower(), 'application/octet-stream')

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

def run_server(port=8000):
    server_address = ('', port)
    httpd = HTTPServer(server_address, TelemetryAPIHandler)
    print(f"Chronon3D Telemetry Server running on http://localhost:{port}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
        httpd.server_close()

if __name__ == '__main__':
    port = 8000
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
    run_server(port)
