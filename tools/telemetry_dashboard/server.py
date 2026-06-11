#!/usr/bin/env python3
"""
Chronon3D Telemetry Dashboard Server
Thin entry point -- all logic lives in the telemetry_server/ package.
"""

import sys
from telemetry_server.flask_app import app, socketio

def run_server(port=8000):
    print(f"Chronon3D Telemetry Flask Server running on http://localhost:{port}")
    socketio.run(app, host='127.0.0.1', port=port, debug=False, allow_unsafe_werkzeug=True)


if __name__ == '__main__':
    port = 8000
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
    run_server(port)
