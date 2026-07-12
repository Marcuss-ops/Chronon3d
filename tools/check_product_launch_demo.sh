#!/usr/bin/env bash
# check_product_launch_demo.sh — Test #1 ProductLaunch end-to-end demo gate.
#
# First-Principles Product Check Test #1 ("demo impossibile da ignorare"):
# runs the canonical chronon3d_cli video render command + reads examples/product_launch.json
# for the expected_output assertion contract + ffprobe-verifies the actual MP4 properties.
#
# Per AGENTS.md §"INFO-level diagnostic style" emits `[INFO] ${GATE_NAME}: ...` additive
# to the canonical `GATE_PASS:` final line on clean state. FAIL path is unchanged
# (GATE_FAIL on stderr + exit 1).
#
# Required CLIs: chronon3d_cli + ffprobe + python3 (all fail-loud at the gate if missing).
# JSON read uses python3 (no jq dep) — more robust than grep+sed.
#
# Override via env: CHRONON_PRODUCT_OUT=/tmp/product-launch.mp4 (default).

set -euo pipefail

GATE_NAME=check_product_launch_demo
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

JSON_SPEC="$REPO_ROOT/examples/product_launch.json"
OUT_MP4="${CHRONON_PRODUCT_OUT:-/tmp/product-launch.mp4}"

# ── Required binaries + python3 + JSON spec ─────────────────────────────────
if ! command -v python3 >/dev/null 2>&1; then
    echo "GATE_FAIL: python3 not in PATH — required for JSON parsing + float math" >&2
    exit 1
fi
if ! command -v ffprobe >/dev/null 2>&1; then
    echo "GATE_FAIL: ffprobe not in PATH — install ffmpeg/ffprobe" >&2
    exit 1
fi

# Locate chronon3d_cli: PATH first, then known CMake build dirs (per repo's preset layout).
CHRONON_CLI=""
for candidate in \
    "$(command -v chronon3d_cli 2>/dev/null || true)" \
    "$REPO_ROOT/build/apps/chronon3d_cli/chronon3d_cli" \
    "$REPO_ROOT/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
    "$REPO_ROOT/build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
        CHRONON_CLI="$candidate"
        break
    fi
done
if [ -z "$CHRONON_CLI" ]; then
    echo "GATE_FAIL: chronon3d_cli not in PATH or any known cmake build dir" >&2
    echo "  remediation: cmake --build <build_dir> --target chronon3d_cli (needs CHRONON3D_BUILD_CONTENT=ON to register ProductLaunch)" >&2
    exit 1
fi
if [ ! -f "$JSON_SPEC" ]; then
    echo "GATE_FAIL: spec file missing at $JSON_SPEC" >&2
    exit 1
fi

# ── Defensive JSON read via python3 (per build-host deps) ───────────────────
read_json_field() {
    # $1=key, prints numeric value or fails loud
    python3 -c "
import json, sys
with open('$JSON_SPEC', 'r') as f: spec = json.load(f)
def find(obj, key):
    if isinstance(obj, dict):
        if key in obj: return obj[key]
        for v in obj.values():
            r = find(v, key)
            if r is not None: return r
    return None
v = find(spec, '$1')
if v is None: sys.exit('GATE_FAIL: $1 not found in $JSON_SPEC')
print(v)
"
}
EXPECTED_DURATION="$(read_json_field duration_seconds)"
EXPECTED_W="$(read_json_field width)"
EXPECTED_H="$(read_json_field height)"
TOLERANCE=0.10  # matches examples/product_launch.json::expected_output.duration_seconds_tolerance

# ── Canonical render command ──────────────────────────────────────────────
# §honesty: the user-literal `chronon render --props` form does NOT exist (no `--props`
# flag in apps/chronon3d_cli/commands.hpp). The canonical real CLI is below; the JSON
# spec is the defensive sidecar that drives the gate's expected_output assertions.
$CHRONON_CLI video ProductLaunch -o "$OUT_MP4" --start 0 --end 90 --fps 30 --motion-blur

# ── ffprobe assertions (single-pass for width + height) ────────────────────
ACTUAL_DURATION=$(ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 "$OUT_MP4" 2>&1 | head -1 | awk '{print $1}')
STREAM_PROPS=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of default=nw=1:nk=1 "$OUT_MP4" 2>&1 | head -2 | tr '\n' ' ')
ACTUAL_W=$(echo "$STREAM_PROPS" | awk '{for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) print $i}' | head -1)
ACTUAL_H=$(echo "$STREAM_PROPS" | awk '{for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) print $i}' | tail -1)

# ── Assertions (fail-loud) using python3 for portable float math ───────────
PASS=1
if ! python3 -c "
import sys
a, b, t = float('${ACTUAL_DURATION:-0}'), $EXPECTED_DURATION, $TOLERANCE
sys.exit(0 if abs(a-b) <= t else 1)
" 2>/dev/null; then
    echo "GATE_FAIL: duration drift > ${TOLERANCE}s (expected $EXPECTED_DURATION actual ${ACTUAL_DURATION:-unset})" >&2
    PASS=0
fi
[ "${ACTUAL_W:-0}" != "$EXPECTED_W" ] && { echo "GATE_FAIL: width mismatch (expected $EXPECTED_W actual ${ACTUAL_W:-unset})" >&2; PASS=0; }
[ "${ACTUAL_H:-0}" != "$EXPECTED_H" ] && { echo "GATE_FAIL: height mismatch (expected $EXPECTED_H actual ${ACTUAL_H:-unset})" >&2; PASS=0; }

# ── Gate verdict ───────────────────────────────────────────────────────────
if [ "$PASS" -eq 1 ]; then
    echo "GATE_PASS: ProductLaunch demo MP4 verified — duration=${ACTUAL_DURATION}s (±${TOLERANCE}s of $EXPECTED_DURATION), resolution=${ACTUAL_W}×${ACTUAL_H}"
    echo "[INFO] ${GATE_NAME}: zero assertion failures on Test #1 (ProductLaunch demo gate holds)"
    exit 0
else
    echo "GATE_FAIL: 1+ assertion(s) failed; remediation: re-check CLI render + asset path + ffprobe availability" >&2
    exit 1
fi
