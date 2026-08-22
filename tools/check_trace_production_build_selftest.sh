#!/usr/bin/env bash
# ============================================================================
# tools/check_trace_production_build_selftest.sh
#
# Regression self-test for the production tracing propagation contract:
#   1. chronon3d publishes the tracing define and Perfetto requirements;
#   2. the video exporter consumes the canonical chronon3d interface;
#   3. the production compile database gate passes on a real tracing build;
#   4. removing one define is reported as TRACE_OFF;
#   5. malformed and missing compile databases fail closed.
#
# Usage:
#   bash tools/check_trace_production_build_selftest.sh [compile_commands.json]
#
# The positive database defaults to the same path as
# check_trace_production_build.sh.  A configured tracing build is therefore
# required for the positive integration assertion; static CMake assertions
# still run when no database is available.
# ============================================================================
set -uo pipefail

GATE_NAME="check_trace_production_build_selftest"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GATE="$SCRIPT_DIR/check_trace_production_build.sh"
COMPILE_COMMANDS="${1:-${CHRONON3D_TRACE_PRODUCTION_COMPILE_COMMANDS:-build/chronon/linux-fast-dev/compile_commands.json}}"

PASS=0
FAIL=0
BLOCKED=0

assert_status() {
    local label="$1" expected="$2" actual="$3"
    if [[ "$expected" == "$actual" ]]; then
        echo "  PASS: $label (exit=$actual)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label (want=$expected got=$actual)"
        FAIL=$((FAIL + 1))
    fi
}

assert_output() {
    local label="$1" pattern="$2" output="$3"
    if grep -Eq "$pattern" <<<"$output"; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label"
        FAIL=$((FAIL + 1))
    fi
}

if [[ ! -r "$GATE" ]]; then
    echo "GATE_FAIL: missing production gate: $GATE" >&2
    exit 2
fi

# ── CMake propagation contract ─────────────────────────────────────────────
# Keep these checks narrow and anchored to the owning target blocks so a
# comment or unrelated target cannot satisfy the regression lock.
python3 - "$ROOT" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
src = (root / "src/CMakeLists.txt").read_text(encoding="utf-8")
video = (root / "apps/chronon3d_cli/cmake/VideoExport.cmake").read_text(encoding="utf-8")

chronon_block = re.search(
    r"add_library\(chronon3d INTERFACE\)(.*?)(?=\n# =+\n# Per-subsystem targets)",
    src,
    re.DOTALL,
)
if not chronon_block:
    print("CMAKE_FAIL: chronon3d INTERFACE block not found")
    raise SystemExit(1)
block = chronon_block.group(1)
required = [
    ("tracing define", r"target_compile_definitions\(chronon3d INTERFACE[\s\S]*?CHRONON3D_ENABLE_TRACING"),
    ("Perfetto interface", r"target_link_libraries\(chronon3d INTERFACE[\s\S]*?unofficial::perfetto::perfetto"),
]
for label, pattern in required:
    if not re.search(pattern, block):
        print(f"CMAKE_FAIL: {label} is not published by chronon3d INTERFACE")
        raise SystemExit(1)
    print(f"  PASS: CMake chronon3d INTERFACE publishes {label}")

if not re.search(
    r"target_link_libraries\(chronon3d_cli_video_export PRIVATE[\s\S]*?\bchronon3d\b",
    video,
):
    print("CMAKE_FAIL: chronon3d_cli_video_export does not consume chronon3d")
    raise SystemExit(1)
print("  PASS: CMake video exporter consumes chronon3d interface")
PY
rc=$?
assert_status "CMake propagation contract" 0 "$rc"

# ── Positive integration check on the real compile database ────────────────
if [[ -f "$ROOT/$COMPILE_COMMANDS" || -f "$COMPILE_COMMANDS" ]]; then
    set +e
    positive_output=$(bash "$GATE" "$COMPILE_COMMANDS" 2>&1)
    positive_rc=$?
    set -e
    assert_status "real production compile database passes" 0 "$positive_rc"
    assert_output "positive gate emits GATE_PASS" '^GATE_PASS:' "$positive_output"
    assert_output "positive gate emits INFO diagnostic" '^\[INFO\] check_trace_production_build:' "$positive_output"

    TMP="$(mktemp -d -t chronon_trace_production_selftest.XXXXXX)"
    trap 'rm -rf "${TMP:-}"' EXIT
    cp "$ROOT/$COMPILE_COMMANDS" "$TMP/compile_commands.json" 2>/dev/null || \
        cp "$COMPILE_COMMANDS" "$TMP/compile_commands.json"
    if [[ -f "$(dirname "$ROOT/$COMPILE_COMMANDS")/CMakeCache.txt" ]]; then
        cp "$(dirname "$ROOT/$COMPILE_COMMANDS")/CMakeCache.txt" "$TMP/CMakeCache.txt"
    elif [[ -f "$(dirname "$COMPILE_COMMANDS")/CMakeCache.txt" ]]; then
        cp "$(dirname "$COMPILE_COMMANDS")/CMakeCache.txt" "$TMP/CMakeCache.txt"
    fi

    # Make one real production source TRACE_OFF while preserving every other
    # compile command. This verifies the gate reads command-level defines,
    # rather than merely trusting CHRONON3D_ENABLE_TRACING in CMakeCache.txt.
    python3 - "$TMP/compile_commands.json" <<'PY'
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
selected = None
for entry in data:
    source = str(entry.get("file", ""))
    if source.endswith("src/render_graph/pipeline/scene.cpp"):
        selected = source
        break
if selected is None:
    for entry in data:
        source = str(entry.get("file", ""))
        if "/src/" in source or source.startswith("src/"):
            selected = source
            break
if selected is None:
    raise SystemExit("no production source found in compile database")

pattern = re.compile(r"(^|[ \t])-DCHRONON3D_ENABLE_TRACING(?:=[^ \t]+)?(?=$|[ \t])")
changed = 0
for entry in data:
    source = str(entry.get("file", ""))
    if source != selected:
        continue
    if isinstance(entry.get("command"), str):
        entry["command"] = pattern.sub(r"\1", entry["command"])
        changed += 1
    if isinstance(entry.get("arguments"), list):
        entry["arguments"] = [
            arg for arg in entry["arguments"]
            if not re.fullmatch(r"-DCHRONON3D_ENABLE_TRACING(?:=.*)?", str(arg))
        ]
        changed += 1
if changed == 0:
    raise SystemExit("selected source has no command/arguments field")
path.write_text(json.dumps(data), encoding="utf-8")
PY
    rc=$?
    assert_status "negative fixture preparation" 0 "$rc"

    if [[ "$rc" -eq 0 ]]; then
        set +e
        negative_output=$(bash "$GATE" "$TMP/compile_commands.json" 2>&1)
        negative_rc=$?
        set -e
        assert_status "missing tracing define fails the gate" 1 "$negative_rc"
        assert_output "negative gate reports TRACE_OFF" 'TRACE_OFF' "$negative_output"
    fi
    rm -rf "$TMP"
    trap - EXIT
else
    echo "  NOT RUN: real compile database not found: $COMPILE_COMMANDS"
    BLOCKED=$((BLOCKED + 1))
fi

# ── Fail-closed input checks ───────────────────────────────────────────────
set +e
missing_output=$(bash "$GATE" "$ROOT/.trace-production-missing.json" 2>&1)
missing_rc=$?
set -e
assert_status "missing compile database fails closed" 2 "$missing_rc"
assert_output "missing database diagnostic is explicit" 'compile_commands\.json not found' "$missing_output"

TMP_BAD="$(mktemp -d -t chronon_trace_production_bad.XXXXXX)"
printf '{\n' > "$TMP_BAD/compile_commands.json"
set +e
malformed_output=$(bash "$GATE" "$TMP_BAD/compile_commands.json" 2>&1)
malformed_rc=$?
set -e
assert_status "malformed compile database fails closed" 2 "$malformed_rc"
assert_output "malformed database diagnostic is explicit" 'cannot parse' "$malformed_output"
rm -rf "$TMP_BAD"

if [[ "$FAIL" -gt 0 ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: $FAIL failed assertion(s), $PASS passed, $BLOCKED blocked" >&2
    exit 1
fi
if [[ "$BLOCKED" -gt 0 ]]; then
    echo "GATE_PASS: ${GATE_NAME}: $PASS assertions passed, $BLOCKED integration assertion(s) NOT RUN"
    echo "[INFO] ${GATE_NAME}: static contract and fail-closed behavior verified"
    exit 0
fi
echo "GATE_PASS: ${GATE_NAME}: $PASS assertions passed"
echo "[INFO] ${GATE_NAME}: CMake propagation and production compile-database regressions covered"
exit 0
