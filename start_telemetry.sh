#!/bin/bash
# Chronon3D Telemetry Dashboard Startup
# Usage: ./start_telemetry.sh [--kill]

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# ── Kill mode ──────────────────────────────────────────────────────────────────
if [ "$1" = "--kill" ]; then
    echo "Killing telemetry services..."
    fuser -k 5005/tcp 2>/dev/null
    fuser -k 5173/tcp 2>/dev/null
    echo "Done."
    exit 0
fi

# ── Kill existing processes on the ports ────────────────────────────────────────
fuser -k 5005/tcp 2>/dev/null
fuser -k 5173/tcp 2>/dev/null
sleep 1

echo "=== Chronon3D Telemetry Dashboard ==="

# ── 1. Flask backend (port 5005) ───────────────────────────────────────────────
echo "[1/2] Starting Flask backend on :5005..."
setsid sh -c "cd '$DIR/tools/telemetry_dashboard' && exec python3 server.py 5005" >/tmp/flask.log 2>&1 &
FLASK_PID=$!

# ── 2. Vite frontend (port 5173) ──────────────────────────────────────────────
echo "[2/2] Starting Vite frontend on :5173..."
setsid sh -c "cd '$DIR/tools/telemetry_dashboard/frontend' && exec node ./node_modules/.bin/vite --port 5173 --host 0.0.0.0" >/tmp/vite.log 2>&1 &
VITE_PID=$!

# ── Wait for both to be ready ──────────────────────────────────────────────────
echo "Waiting for services..."
for i in $(seq 1 20); do
    FLASK_OK=$(curl -s --max-time 2 -o /dev/null -w "%{http_code}" http://localhost:5005/api/runs 2>/dev/null)
    VITE_OK=$(curl -s --max-time 2 -o /dev/null -w "%{http_code}" http://localhost:5173/ 2>/dev/null)
    if [ "$FLASK_OK" = "200" ] && [ "$VITE_OK" = "200" ]; then
        break
    fi
    sleep 1
done

# ── Status ─────────────────────────────────────────────────────────────────────
echo ""
if [ "$FLASK_OK" = "200" ]; then
    echo "  Backend  [OK]  http://localhost:5005"
else
    echo "  Backend  [FAIL] see /tmp/flask.log"
fi
if [ "$VITE_OK" = "200" ]; then
    echo "  Frontend [OK]  http://localhost:5173"
else
    echo "  Frontend [FAIL] see /tmp/vite.log"
fi
echo ""
echo "  Stop:  ./start_telemetry.sh --kill"
