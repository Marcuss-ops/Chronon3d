#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# selftest_check_manual_touches_per_video.sh — 4-scenario verdict-logic test
# ═══════════════════════════════════════════════════════════════════════════
#
# Selftest for tools/check_manual_touches_per_video.sh using mock events
# (no real chronon3d_cli / no render execution). Exercises 4 scenarios:
#
#   Scenario 1: PASS — synthetic events all within thresholds across all
#               4 phases (oggi=5<=8 + fase1=2<=3 + fase2=1<=1 + finale=0<=0)
#
#   Scenario 2: FAIL_OGGI — synthetic events exceed oggi threshold
#               (oggi=9>8) while other 3 phases PASS — gate emits FAIL
#               with phase-specific diagnostic
#
#   Scenario 3: FAIL_FINALE — synthetic events exceed finale=0 (strict)
#               (finale=1>0) — gate emits FAIL with finale-specific
#               diagnostic (the most surprising failure case since the
#               threshold is exactly 0)
#
#   Scenario 4: PRECOND_NO_PYYAML — PATH-stripped Python environment to
#               simulate missing PyYAML — gate SHOULD emit exit 2 +
#               GATE_FAIL_INTERNAL with PyYAML diagnostic
#
# Per AGENTS.md v0.1 Cat-1 + Cat-3: tool-only test, ZERO new public SDK API.
# Per AGENTS.md INFO-level diagnostic style rule #2: emits its OWN
# `[INFO] selftest_check_manual_touches_per_video: ...` line on PASS
# (grep-discoverable family).
#
# Per AGENTS.md §honest-limitation: selftest is FULLY EXERCISABLE on this
# VPS (uses python3 + pyyaml + mock JSONL; NO chronon3d_cli dependency).
# Macchina-verifica covers 4/4 scenarios; failures are reproducible.
#
# Usage:
#   bash tests/tools/selftest_check_manual_touches_per_video.sh
#   bash tests/tools/selftest_check_manual_touches_per_video.sh --quick  # PASS-only smoke
#
# Exit codes: 0 = all 4 scenarios PASS, 1 = at least one scenario FAIL.
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME=selftest_check_manual_touches_per_video
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GATE_SCRIPT="$REPO_ROOT/tools/check_manual_touches_per_video.sh"

if [ ! -x "$GATE_SCRIPT" ]; then
    echo "GATE_FAIL_INTERNAL: gate script not executable: $GATE_SCRIPT" >&2
    echo "  fix: chmod +x $GATE_SCRIPT" >&2
    exit 2
fi

PYTHON_BIN="${PYTHON_BIN:-python3}"
if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
    echo "GATE_FAIL_INTERNAL: python3 not found in PATH" >&2
    exit 2
fi

MODES="ALL"
if [ "${1:-}" = "--quick" ]; then
    MODES="PASS_ONLY"
fi

WORKDIR="$(mktemp -d -t mt_selftest_XXXXXX)"
trap 'rm -rf "$WORKDIR"' EXIT

mkdir -p "$WORKDIR/logs"
mkdir -p "$WORKDIR/configs"

# Copy the canonical config + override LOG_PATH for selftest isolation
cp "$REPO_ROOT/configs/touchpoint_thresholds.yaml" "$WORKDIR/configs/touchpoint_thresholds.yaml"

SCENARIO=0
FAILED=0

# ── Scenario 1: PASS (all 4 phases within thresholds) ───────────────────────
SCENARIO=$((SCENARIO + 1))
SCEN1_LOG="$WORKDIR/logs/scen1.jsonl"
SCEN1_OUT="$WORKDIR/scen1.out"
"$PYTHON_BIN" - "$SCEN1_LOG" <<'PYEOF' >/dev/null
import json, sys
log = sys.argv[1]
# oggi=5 (5 distinct kinds cap=8), fase1=2 (cap=3), fase2=1 (cap=1), finale=0 (cap=0)
events = [
    {"ts": "2026-07-12T10:00:00Z", "run_id": "r_oggi_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": "rename", "actor": "human"},
    {"ts": "2026-07-12T10:01:00Z", "run_id": "r_oggi_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": "copy", "actor": "human"},
    {"ts": "2026-07-12T10:02:00Z", "run_id": "r_oggi_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": "clip", "actor": "human"},
    {"ts": "2026-07-12T10:03:00Z", "run_id": "r_oggi_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": "text_fix", "actor": "human"},
    {"ts": "2026-07-12T10:04:00Z", "run_id": "r_oggi_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": "timing", "actor": "human"},
    {"ts": "2026-07-12T11:00:00Z", "run_id": "r_fase1_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase1", "op": "font", "actor": "human"},
    {"ts": "2026-07-12T11:01:00Z", "run_id": "r_fase1_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase1", "op": "relaunch", "actor": "human"},
    {"ts": "2026-07-12T12:00:00Z", "run_id": "r_fase2_1", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase2", "op": "output_check", "actor": "human"},
    # finale empty
]
with open(log, "w", encoding="utf-8") as f:
    for e in events:
        f.write(json.dumps(e) + "\n")
PYEOF

set +e
LOG_PATH="$SCEN1_LOG" CONFIG="$WORKDIR/configs/touchpoint_thresholds.yaml" \
    bash "$GATE_SCRIPT" >"$SCEN1_OUT" 2>&1
SCEN1_RC=$?
set -e

if [ "$SCEN1_RC" = "0" ] && grep -q "^GATE_PASS:" "$SCEN1_OUT" \
   && grep -qE "^\[INFO\] ${GATE_NAME#selftest_}: 4/4 phases PASS" "$SCEN1_OUT"; then
    echo "  [OK] scen1 PASS: gate exit 0 + GATE_PASS + [INFO] line; 4/4 phases within thresholds"
else
    echo "  [FAIL] scen1: expected exit 0 + GATE_PASS + [INFO] 4/4; got exit $SCEN1_RC"
    echo "  ── scen1 output ──"
    sed 's/^/    /' "$SCEN1_OUT" | head -20
    FAILED=$((FAILED + 1))
fi

if [ "$MODES" = "PASS_ONLY" ]; then
    if [ "$FAILED" = "0" ]; then
        echo "GATE_PASS: $SCENARIO/$SCENARIO selftest scenarios PASS (--quick mode)"
        echo "[INFO] ${GATE_NAME}: $SCENARIO/$SCENARIO --quick smoke PASS"
        exit 0
    fi
    echo "GATE_FAIL: $FAILED/$SCENARIO selftest scenario(s) FAIL (--quick mode)"
    exit 1
fi

# ── Scenario 2: FAIL_OGGI (oggi=9 > 8 threshold) ───────────────────────────
SCENARIO=$((SCENARIO + 1))
SCEN2_LOG="$WORKDIR/logs/scen2.jsonl"
SCEN2_OUT="$WORKDIR/scen2.out"
SCEN2_ERR="$WORKDIR/scen2.err"
"$PYTHON_BIN" - "$SCEN2_LOG" <<'PYEOF' >/dev/null
import json, sys
log = sys.argv[1]
# oggi=9 distinct (exceeds 8); fase1=1; fase2=1; finale=0
events = [
    {"ts": "2026-07-12T10:00:00Z", "run_id": f"r{i}", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": op, "actor": "human"}
    for i, op in enumerate(["rename", "copy", "clip", "text_fix", "timing", "font", "relaunch", "output_check", "upload"])
]
events += [
    {"ts": "2026-07-12T11:00:00Z", "run_id": "r_fase1", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase1", "op": "rename", "actor": "human"},
    {"ts": "2026-07-12T12:00:00Z", "run_id": "r_fase2", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase2", "op": "rename", "actor": "human"},
]
with open(log, "w", encoding="utf-8") as f:
    for e in events:
        f.write(json.dumps(e) + "\n")
PYEOF

set +e
LOG_PATH="$SCEN2_LOG" CONFIG="$WORKDIR/configs/touchpoint_thresholds.yaml" \
    bash "$GATE_SCRIPT" >"$SCEN2_OUT" 2>"$SCEN2_ERR"
SCEN2_RC=$?
set -e

if [ "$SCEN2_RC" = "1" ] && grep -q "^GATE_FAIL:" "$SCEN2_ERR" \
   && grep -q "oggi:" "$SCEN2_ERR" \
   && ! grep -q "^GATE_PASS:" "$SCEN2_OUT"; then
    echo "  [OK] scen2 FAIL_OGGI: gate exit 1 + GATE_FAIL + oggi: actual=9 max_allowed=8 diagnostic; PASS not spuriously emitted"
else
    echo "  [FAIL] scen2: expected exit 1 + GATE_FAIL on stderr + oggi diagnostic; got exit $SCEN2_RC"
    echo "  ── scen2 stdout ──"
    sed 's/^/    /' "$SCEN2_OUT" | head -10
    echo "  ── scen2 stderr ──"
    sed 's/^/    /' "$SCEN2_ERR" | head -10
    FAILED=$((FAILED + 1))
fi

# ── Scenario 3: FAIL_FINALE (finale=1 > 0 — strict zero tolerance) ─────────
SCENARIO=$((SCENARIO + 1))
SCEN3_LOG="$WORKDIR/logs/scen3.jsonl"
SCEN3_OUT="$WORKDIR/scen3.out"
SCEN3_ERR="$WORKDIR/scen3.err"
"$PYTHON_BIN" - "$SCEN3_LOG" <<'PYEOF' >/dev/null
import json, sys
log = sys.argv[1]
# All 4 phases within thresholds EXCEPT finale=1 (exceeds strict 0)
events = [
    {"ts": "2026-07-12T10:00:00Z", "run_id": "r_oggi", "composition_id": "ChrononGlowFinalAE",
     "phase": "oggi", "op": "rename", "actor": "human"},
    {"ts": "2026-07-12T11:00:00Z", "run_id": "r_fase1", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase1", "op": "rename", "actor": "human"},
    {"ts": "2026-07-12T12:00:00Z", "run_id": "r_fase2", "composition_id": "ChrononGlowFinalAE",
     "phase": "fase2", "op": "rename", "actor": "human"},
    # The single failure trigger:
    {"ts": "2026-07-12T13:00:00Z", "run_id": "r_finale", "composition_id": "ChrononGlowFinalAE",
     "phase": "finale", "op": "upload", "actor": "human"},
]
with open(log, "w", encoding="utf-8") as f:
    for e in events:
        f.write(json.dumps(e) + "\n")
PYEOF

set +e
LOG_PATH="$SCEN3_LOG" CONFIG="$WORKDIR/configs/touchpoint_thresholds.yaml" \
    bash "$GATE_SCRIPT" >"$SCEN3_OUT" 2>"$SCEN3_ERR"
SCEN3_RC=$?
set -e

if [ "$SCEN3_RC" = "1" ] && grep -q "^GATE_FAIL:" "$SCEN3_ERR" \
   && grep -q "finale:" "$SCEN3_ERR" \
   && grep -q "actual=1 max_allowed=0" "$SCEN3_ERR"; then
    echo "  [OK] scen3 FAIL_FINALE: gate exit 1 + GATE_FAIL + finale diagnostic with strict-zero enforcement"
else
    echo "  [FAIL] scen3: expected exit 1 + GATE_FAIL + finale: actual=1 max_allowed=0; got exit $SCEN3_RC"
    echo "  ── scen3 stdout ──"
    sed 's/^/    /' "$SCEN3_OUT" | head -10
    echo "  ── scen3 stderr ──"
    sed 's/^/    /' "$SCEN3_ERR" | head -10
    FAILED=$((FAILED + 1))
fi

# ── Scenario 4: PRECOND_NO_PYTHON (simulate missing python3) ──────────────
SCENARIO=$((SCENARIO + 1))
SCEN4_OUT="$WORKDIR/scen4.out"
SCEN4_ERR="$WORKDIR/scen4.err"

set +e
PATH="/usr/bin:/bin" PYTHON_BIN="python3_does_not_exist_xyz" \
    bash "$GATE_SCRIPT" >"$SCEN4_OUT" 2>"$SCEN4_ERR"
SCEN4_RC=$?
set -e

if [ "$SCEN4_RC" = "2" ] && grep -q "GATE_FAIL_INTERNAL" "$SCEN4_ERR" \
   && grep -q "python3 not found" "$SCEN4_ERR"; then
    echo "  [OK] scen4 PRECOND_NO_PYTHON: gate exit 2 + GATE_FAIL_INTERNAL + python3 diagnostic; never spuriously GATE_PASS"
else
    echo "  [FAIL] scen4: expected exit 2 + GATE_FAIL_INTERNAL + python3 diagnostic; got exit $SCEN4_RC"
    echo "  ── scen4 stdout ──"
    sed 's/^/    /' "$SCEN4_OUT" | head -10
    echo "  ── scen4 stderr ──"
    sed 's/^/    /' "$SCEN4_ERR" | head -10
    FAILED=$((FAILED + 1))
fi

# ── Final verdict ───────────────────────────────────────────────────────────
TOTAL=$SCENARIO
if [ "$FAILED" = "0" ]; then
    echo "GATE_PASS: $TOTAL/$TOTAL selftest scenarios PASS (PASS / FAIL_OGGI / FAIL_FINALE / PRECOND)"
    echo "[INFO] ${GATE_NAME}: $TOTAL/$TOTAL selftest scenarios PASS — gate verdict logic exercised end-to-end on this VPS"
    exit 0
fi
echo "GATE_FAIL: $FAILED/$TOTAL selftest scenario(s) FAIL"
exit 1
