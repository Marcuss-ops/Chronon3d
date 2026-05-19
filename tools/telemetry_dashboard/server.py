#!/usr/bin/env python3
"""
Chronon3D Telemetry Dashboard Server
Thin entry point -- all logic lives in the telemetry_server/ package.
"""

import sys
from http.server import HTTPServer
from telemetry_server.handler import TelemetryAPIHandler


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
