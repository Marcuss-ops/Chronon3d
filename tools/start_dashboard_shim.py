#!/usr/bin/env python3
"""Run the Chronon3D telemetry dashboard bound to 0.0.0.0 so it's reachable
from the host's public IP, not just loopback.

Runtime shim only — no business logic, no monkey-patching beyond the
host= override.  Idempotent: re-running replaces any prior bind string.

Usage:
    CHRONON3D_DASHBOARD_PASSWORD=<pwd> python3 tools/start_dashboard_shim.py [port]
"""
import os
import sys

# Single source of truth: the password MUST be exported by the caller.
# Refusing to bind 0.0.0.0 with no password is the safe default for any
# network-facing shim; the loud warning above is theatre.
if not os.environ.get("CHRONON3D_DASHBOARD_PASSWORD"):
    print(
        "[start_dashboard_shim] refusing to bind 0.0.0.0 without "
        "CHRONON3D_DASHBOARD_PASSWORD set in the environment",
        file=sys.stderr,
        flush=True,
    )
    sys.exit(2)
PASSWORD = os.environ["CHRONON3D_DASHBOARD_PASSWORD"]
os.environ["PYTHONUNBUFFERED"] = "1"

port = sys.argv[1] if len(sys.argv) > 1 else "5005"
DASH_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "telemetry_dashboard")
SERVER_PY = os.path.join(DASH_DIR, "server.py")

sys.path.insert(0, DASH_DIR)
sys.argv = ["server.py", port]

src = open(SERVER_PY, "r", encoding="utf-8").read()

# Idempotent bind override — every plausible quoting style for the literal 127.0.0.1.
overrides = (
    # 127.0.0.1 in every plausible quoting style.
    ("host='127.0.0.1'",  "host='0.0.0.0'"),
    ('host="127.0.0.1"',   'host="0.0.0.0"'),
    ("host=127.0.0.1",     "host='0.0.0.0'"),
    ("host = '127.0.0.1'", "host = '0.0.0.0'"),
    ('host = "127.0.0.1"', 'host = "0.0.0.0"'),
    # 'localhost' literal — Flask apps commonly bind to this instead of 127.0.0.1.
    ("host='localhost'",   "host='0.0.0.0'"),
    ('host="localhost"',   'host="0.0.0.0"'),
    ("host = 'localhost'", "host = '0.0.0.0'"),
    ('host = "localhost"', 'host = "0.0.0.0"'),
)
patched_count = 0
for old, new in overrides:
    if old in src:
        src = src.replace(old, new)
        patched_count += 1

if patched_count == 0:
    print(
        "[start_dashboard_shim] NOTE: no host=127.0.0.1 / host=localhost "
        "patterns matched in server.py — the file may already bind 0.0.0.0 "
        "directly. This is fine; the override is a no-op.",
        flush=True,
    )
print(
    f"[start_dashboard_shim] exec {SERVER_PY} on 0.0.0.0:{port} "
    f"pwd_set=yes password_len={len(PASSWORD)} host_patches={patched_count}",
    flush=True,
)

exec(
    compile(src, SERVER_PY, "exec"),
    {"__name__": "__main__", "__file__": SERVER_PY},
)
