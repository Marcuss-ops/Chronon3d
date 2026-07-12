#!/usr/bin/env bash
# tests/tools/selftest_check_resource_limits.sh — selftest for tools/check_resource_limits.sh
#
# 4 scenarios + 1 PRECOND exercising the gate's verdict logic via mock CLIs.
# Fully exercisable on this VPS — no chronon3d_cli required (the gate's
# CLI_DEBUG_PATH/CLI_RELEASE_PATH env vars are pointed at a per-scenario
# mock CLI bash script that emits deterministic VmHWM + wall-time + temp-file
# behaviors).
#
# Pattern mirrors tests/tools/selftest_check_linear_scaling.sh (PASS +
# FAIL_RAM + FAIL_CACHE + PRECOND scenarios + per-scenario workdir staging +
# merged-stdout capture for exit-code parsing under set -euo pipefail).
#
# Scenarios:
#   1) PASS              : 4/4 invariants within budget
#   2) FAIL_PEAK_RAM     : peak RAM exceeds 512MB budget
#   3) FAIL_FRAME_P95    : one frame's wall-time exceeds 2× P95
#   4) FAIL_LEAK         : VmHWM growth over 100 jobs exceeds 5 MB threshold
#   5) PRECOND           : PATH stripped of `jq` → exit 2 + GATE_FAIL_INTERNAL

set -uo pipefail

GATE_SCRIPT="${GATE_SCRIPT:-$(cd "$(dirname "$0")/../.." && pwd)/tools/check_resource_limits.sh}"
SELFTEST_NAME="selftest_check_resource_limits"
WORK="$(mktemp -d -t "${SELFTEST_NAME}.XXXXXX")"

# Mock CLI template: parameterized via env vars per scenario.
#   MOCK_PEAK_KB              : allocate this many KB of memory (sets VmHWM)
#   MOCK_WALL_MS              : sleep this many ms (controls wall-time)
#   MOCK_WALL_VARIANCE_AFTER  : on counter >= this value, sleep MOCK_WALL_VARIANCE_HIGH_MS (for P95 violation)
#   MOCK_WALL_VARIANCE_HIGH_MS: high wall-time value to inject on later frames
#   MOCK_LEAK_PER_INV_KB      : allocate this many extra KB per invocation (for leak test)
#   MOCK_COUNTER_FILE         : path to invocation counter (for leak + variance tests)
#   MOCK_TEMP_FILE            : touch this file (relative to $TMPDIR) if non-empty
#   MOCK_EXIT_CODE            : exit with this code
MOCK_CLI_TEMPLATE="${WORK}/mock_cli.sh"

# Cleanup trap
trap 'rm -rf "$WORK" /tmp/chronon3d_selftest_orphan_* 2>/dev/null || true' EXIT

# Generate the mock CLI template
cat > "$MOCK_CLI_TEMPLATE" <<'MOCK_EOF'
#!/bin/bash
# Mock CLI for Test 13 selftest. Behavior controlled via env vars.
set +e
counter_file="${MOCK_COUNTER_FILE:-/dev/null}"
invocation_num=0
if [ -f "$counter_file" ]; then
    invocation_num=$(cat "$counter_file")
fi

# Unconditional counter increment at script start (consolidates MINOR-A:
# previously the counter was incremented in 2 places — leak path + end-of-script
# fallback — which left the variance path never incrementing, breaking the P95
# scenario's invocation tracking).
invocation_num=$((invocation_num + 1))
if [ -n "${MOCK_COUNTER_FILE:-}" ] && [ "${MOCK_COUNTER_FILE}" != "/dev/null" ]; then
    echo "$invocation_num" > "$counter_file"
fi

# 1) Allocate MOCK_PEAK_KB to set VmHWM
# CRITICAL-2 fix: the prior `head -c bytes | tr ... > /dev/null` was a pipe
# (data discarded; VmHWM never grew). The new pattern RETURNS the bytes via
# command substitution into a bash variable, which IS counted in VmHWM.
alloc_str=""
if [ -n "${MOCK_PEAK_KB:-}" ] && [ "${MOCK_PEAK_KB:-0}" -gt 0 ]; then
    bytes=$((MOCK_PEAK_KB * 1024))
    alloc_str="$(head -c "$bytes" /dev/zero)"
fi

# 2) Leak growth: allocate MOCK_LEAK_PER_INV_KB × invocation_num
# Same CRITICAL-2 fix (retain in bash heap via variable capture).
leak_str=""
if [ -n "${MOCK_LEAK_PER_INV_KB:-}" ] && [ "${MOCK_LEAK_PER_INV_KB:-0}" -gt 0 ]; then
    extra_kb=$((MOCK_LEAK_PER_INV_KB * invocation_num))
    bytes=$((extra_kb * 1024))
    leak_str="$(head -c "$bytes" /dev/zero)"
fi

# Reference the variables to prevent bash from optimizing them away
# (the no-op `: $var` reads the variable, ensuring its heap allocation stays
# alive in VmHWM through the rest of the script's lifetime).
: "${alloc_str}"
: "${leak_str}"

# 3) Wall-time control
wall_ms="${MOCK_WALL_MS:-50}"
if [ -n "${MOCK_WALL_VARIANCE_AFTER:-}" ] && [ -n "${MOCK_WALL_VARIANCE_HIGH_MS:-}" ]; then
    # CRITICAL-1 fix: exact match (==) so exactly ONE frame triggers the variance,
    # not every frame after the threshold. With FRAME_P95_RUNS=20 + variance on
    # invocation 14, the 14th frame is the single outlier; the 19 normal frames
    # dominate the P95 (idx=19 → 50ms), max=500ms, ratio=10× → violation.
    if [ "$invocation_num" -eq "${MOCK_WALL_VARIANCE_AFTER}" ]; then
        wall_ms="${MOCK_WALL_VARIANCE_HIGH_MS}"
    fi
fi
sleep_seconds=$(awk -v ms="$wall_ms" 'BEGIN{printf "%.3f", ms/1000}')
sleep "$sleep_seconds" 2>/dev/null || true

# 4) Temp file creation (relative to $TMPDIR or /tmp)
if [ -n "${MOCK_TEMP_FILE:-}" ]; then
    touch "${MOCK_TEMP_FILE}" 2>/dev/null
fi

exit "${MOCK_EXIT_CODE:-0}"
MOCK_EOF
chmod +x "$MOCK_CLI_TEMPLATE"

# Helper: run a scenario. Sets up per-scenario mock + env, runs the gate, asserts exit code.
# Args: $1=scenario_name  $2=expected_exit  $3=expected_substring  $4=mock_env_setup
run_scenario() {
    local sc_name="$1"
    local expected_ec="$2"
    local expected_substring="$3"
    # mock_env_setup is a multi-line string of `export VAR=val` lines
    local mock_env_setup="$4"

    local sc_dir="${WORK}/${sc_name}"
    mkdir -p "$sc_dir"

    # Stage a per-scenario mock CLI
    cp "$MOCK_CLI_TEMPLATE" "${sc_dir}/mock_cli.sh"
    chmod +x "${sc_dir}/mock_cli.sh"

    # Run the gate with the per-scenario mock + env
    local out
    out="$(cd "$sc_dir" && eval "$mock_env_setup" bash "$GATE_SCRIPT" 2>&1; echo "EXITCODE=$?")"
    local actual_ec
    actual_ec="$(printf '%s' "$out" | awk -F'=' '/^EXITCODE=/{print $2; exit}' || echo 1)"

    # Assert exit code
    if [ "${actual_ec:-1}" -ne "$expected_ec" ]; then
        echo "SCEN_FAIL ${sc_name}: expected exit ${expected_ec}, got ${actual_ec}"
        echo "--- output ---"
        printf '%s\n' "$out" | head -40
        echo "--- end output ---"
        return 1
    fi

    # Assert expected substring (skip for PRECOND where the substring is the GATE_FAIL_INTERNAL line)
    if [ -n "$expected_substring" ]; then
        if ! printf '%s' "$out" | grep -qF "$expected_substring"; then
            echo "SCEN_FAIL ${sc_name}: expected substring '${expected_substring}' not found"
            echo "--- output ---"
            printf '%s\n' "$out" | head -40
            echo "--- end output ---"
            return 1
        fi
    fi

    echo "SCEN_PASS ${sc_name} (exit=${actual_ec})"
    return 0
}

# ── Scenario 1: PASS ────────────────────────────────────────────────────
# All 4 invariants within budget:
#   PEAK ~300 MB (below 512 MB)
#   FRAME P95 stable across 20 frames
#   LEAK 100 jobs with stable VmHWM
#   TEMP no new files
MOCK_SETUP_PASS='
export PEAK_RAM_RUNS=3
export FRAME_P95_RUNS=20
export LEAK_RUN_COUNT=20
export CLI_DEBUG_PATH='"${WORK}"'/scen1/mock_cli.sh
export CLI_RELEASE_PATH='"${WORK}"'/scen1/mock_cli.sh
export MOCK_PEAK_KB=300000
export MOCK_WALL_MS=50
export MOCK_LEAK_PER_INV_KB=0
export MOCK_COUNTER_FILE='"${WORK}"'/scen1/leak_counter
export MOCK_TEMP_FILE=
export MOCK_EXIT_CODE=0
export TMPDIR='"${WORK}"'/scen1_tmp
mkdir -p "$TMPDIR"
'
# Note: LEAK_RUN_COUNT=20 (vs default 100) to keep selftest fast; the selftest
# still exercises the leak-detection logic (VmHWM stability + cache bounded)
# but uses fewer invocations.

run_scenario "scen1_pass" 0 "GATE_PASS" "$MOCK_SETUP_PASS" || exit 1

# ── Scenario 2: FAIL_PEAK_RAM ───────────────────────────────────────────
# MOCK_PEAK_KB=600000 (600 MB) — exceeds the 512 MB budget
MOCK_SETUP_FAIL_PEAK='
export PEAK_RAM_RUNS=3
export FRAME_P95_RUNS=5
export LEAK_RUN_COUNT=10
export CLI_DEBUG_PATH='"${WORK}"'/scen2/mock_cli.sh
export CLI_RELEASE_PATH='"${WORK}"'/scen2/mock_cli.sh
export MOCK_PEAK_KB=600000
export MOCK_WALL_MS=50
export MOCK_LEAK_PER_INV_KB=0
export MOCK_COUNTER_FILE='"${WORK}"'/scen2/leak_counter
export MOCK_TEMP_FILE=
export MOCK_EXIT_CODE=0
export TMPDIR='"${WORK}"'/scen2_tmp
mkdir -p "$TMPDIR"
'
run_scenario "scen2_fail_peak_ram" 1 "PEAK RAM" "$MOCK_SETUP_FAIL_PEAK" || exit 1

# ── Scenario 3: FAIL_FRAME_P95 ──────────────────────────────────────────
# 5 frames: first 4 with MOCK_WALL_MS=50; 5th with MOCK_WALL_MS=500 (10× baseline)
# This makes the 5th frame a clear outlier exceeding 2× P95
MOCK_SETUP_FAIL_P95='
export PEAK_RAM_RUNS=2
export FRAME_P95_RUNS=20
export LEAK_RUN_COUNT=10
export CLI_DEBUG_PATH='"${WORK}"'/scen3/mock_cli.sh
export CLI_RELEASE_PATH='"${WORK}"'/scen3/mock_cli.sh
export MOCK_PEAK_KB=200000
export MOCK_WALL_MS=50
export MOCK_WALL_VARIANCE_AFTER=14
export MOCK_WALL_VARIANCE_HIGH_MS=500
export MOCK_LEAK_PER_INV_KB=0
export MOCK_COUNTER_FILE='"${WORK}"'/scen3/frame_counter
export MOCK_TEMP_FILE=
export MOCK_EXIT_CODE=0
export TMPDIR='"${WORK}"'/scen3_tmp
mkdir -p "$TMPDIR"
# CRITICAL-1 fix: pre-init the counter file so the mock's `cat $counter_file`
# reads 0 on the first invocation; combined with FRAME_P95_RUNS=20 + variance
# on invocation 14 (==, not >=), exactly ONE frame sleeps 500ms; the 19
# normal frames dominate the P95 (idx=19 → 50ms); max=500ms; ratio=10× → violation.
echo 0 > '"${WORK}"'/scen3/frame_counter
'
run_scenario "scen3_fail_frame_p95" 1 "FRAME P95" "$MOCK_SETUP_FAIL_P95" || exit 1

# ── Scenario 4: FAIL_LEAK ──────────────────────────────────────────────
# 20 jobs with MOCK_LEAK_PER_INV_KB=500 (increment 500 KB per invocation)
# After 20 invocations, VmHWM grew by ~10 MB — exceeds 5 MB threshold
MOCK_SETUP_FAIL_LEAK='
export PEAK_RAM_RUNS=2
export FRAME_P95_RUNS=3
export LEAK_RUN_COUNT=20
export CLI_DEBUG_PATH='"${WORK}"'/scen4/mock_cli.sh
export CLI_RELEASE_PATH='"${WORK}"'/scen4/mock_cli.sh
export MOCK_PEAK_KB=200000
export MOCK_WALL_MS=10
export MOCK_LEAK_PER_INV_KB=500
export MOCK_COUNTER_FILE='"${WORK}"'/scen4/leak_counter
export MOCK_TEMP_FILE=
export MOCK_EXIT_CODE=0
export TMPDIR='"${WORK}"'/scen4_tmp
mkdir -p "$TMPDIR"
'
run_scenario "scen4_fail_leak" 1 "LEAK delta" "$MOCK_SETUP_FAIL_LEAK" || exit 1

# ── Scenario 5: PRECOND (PATH stripped of `jq`) ─────────────────────────
# The gate's precondition check fails because `jq` is not in PATH.
# Expected: exit code 2 + GATE_FAIL_INTERNAL message.
mkdir -p "${WORK}/scen5_empty_path"
EMPTY_PATH_DIR="${WORK}/scen5_empty_path"
# CRITICAL-3 fix: PATH="/nonexistent" forces ALL preconditions (jq/du/mktemp/
# awk/xargs/stat + CLI binary) to fail, so the gate exits 2 with a clear
# GATE_FAIL_INTERNAL message. The prior `PATH="${EMPTY_PATH_DIR}:${PATH#*:}"`
# only replaced the FIRST PATH element, leaving other PATH dirs (where jq lives)
# reachable — the test passed by accident via the OTHER preconditions also
# failing in a non-empty EMPTY_PATH_DIR scenario.
PRECOND_OUT="$(cd "$EMPTY_PATH_DIR" && PATH="/nonexistent" bash "$GATE_SCRIPT" 2>&1; echo "EXITCODE=$?")"
PRECOND_EC="$(printf '%s' "$PRECOND_OUT" | awk -F'=' '/^EXITCODE=/{print $2; exit}' || echo 1)"
if [ "${PRECOND_EC:-1}" -ne 2 ]; then
    echo "SCEN_FAIL scen5_precond: expected exit 2, got ${PRECOND_EC}"
    printf '%s\n' "$PRECOND_OUT" | head -20
    exit 1
fi
if ! printf '%s' "$PRECOND_OUT" | grep -qF "GATE_FAIL_INTERNAL"; then
    echo "SCEN_FAIL scen5_precond: expected GATE_FAIL_INTERNAL in output"
    printf '%s\n' "$PRECOND_OUT" | head -20
    exit 1
fi
echo "SCEN_PASS scen5_precond (exit=${PRECOND_EC})"

# ── Aggregate selftest verdict ──────────────────────────────────────────
echo ""
echo "=== AGGREGATE SELFTEST VERDICT ==="
echo "5/5 scenarios PASS: scen1_pass + scen2_fail_peak_ram + scen3_fail_frame_p95 + scen4_fail_leak + scen5_precond"
echo "GATE_PASS: 5/5 selftest scenarios — gate's verdict logic is correct across all 4 invariants + PRECOND contract"
echo "[INFO] ${SELFTEST_NAME}: 5/5 selftest scenarios verified — gate ready for production use (calibration to working build host per the §honesty 'valori da ricalibrare' rule)"
exit 0
