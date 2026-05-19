import os
import sys
import json
import sqlite3
import urllib.parse
from http.server import HTTPServer, BaseHTTPRequestHandler

# Resolve SQLite database path in home directory
DB_PATH = os.path.join(os.path.expanduser('~'), '.chronon3d', 'telemetry', 'chronon3d_render_history.sqlite')

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

    def get_db_connection(self):
        if not os.path.exists(DB_PATH):
            return None
        conn = sqlite3.connect(DB_PATH)
        conn.row_factory = sqlite3.Row
        return conn

    def handle_get_runs(self):
        conn = self.get_db_connection()
        if not conn:
            self.send_json({"error": "Database not found. Render something first!"}, 404)
            return

        try:
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM render_runs ORDER BY finished_at_iso DESC")
            runs = [dict(row) for row in cursor.fetchall()]
            self.send_json(runs)
        except Exception as e:
            self.send_json({"error": str(e)}, 500)
        finally:
            conn.close()

    def handle_get_run_detail(self, run_id):
        conn = self.get_db_connection()
        if not conn:
            self.send_json({"error": "Database not found."}, 404)
            return

        try:
            cursor = conn.cursor()
            
            # Fetch run details
            cursor.execute("SELECT * FROM render_runs WHERE run_id = ?", (run_id,))
            run_row = cursor.fetchone()
            if not run_row:
                self.send_json({"error": "Run not found"}, 404)
                return
            
            run_detail = dict(run_row)

            # Fetch frames
            cursor.execute("SELECT * FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC", (run_id,))
            frames = [dict(row) for row in cursor.fetchall()]

            # Fetch phase events
            cursor.execute("SELECT * FROM render_phase_events WHERE run_id = ? ORDER BY duration_ms DESC", (run_id,))
            phases = [dict(row) for row in cursor.fetchall()]

            # Fetch counters
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
            conn.close()

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

        conn = self.get_db_connection()
        if not conn:
            self.send_json({"error": "Database not found."}, 404)
            return

        try:
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
            conn.close()

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
