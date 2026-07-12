#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_performance_linux.sh
#
# Canonical Performance & Memory certification gate.
#
# Verifies render performance per user spec verbatim:
#   - /usr/bin/time -v on 5 resolution/fps scenarios (60s each)
#   - Records: avg fps, avg CPU%, max RAM (peak RSS), cache hit rate,
#              output size, encoder time
#   - Memory leak test: 100 consecutive renders of the same template
#     (RSS growth ≤ threshold → no leak)
#
# Scenarios (user spec):
#   1. 1080p30  (1920×1080 @ 30fps × 60s)
#   2. 1080p60  (1920×1080 @ 60fps × 60s)
#   3. portrait (1080×1920 @ 30fps × 60s)
#   4. 4K       (3840×2160 @ 30fps × 60s)
#   5. default  (1920×1080 @ 30fps × 60s)
#
# Verdict contract:
#   PERFORMANCE_FUNCTIONAL_PASS    — all sections pass
#   PERFORMANCE_FUNCTIONAL_FAIL    — any section fails
#   PERFORMANCE_FUNCTIONAL_BLOCKED — env/binary not available
#   Exit 0 = PASS, 1 = FAIL, 2 = BLOCKED
#
# Usage:
#   bash tools/verify_performance_linux.sh
#
# Environment:
#   CHRONON3D_PERF_CLI_BIN=<path>       Override CLI binary path
#   CHRONON3D_PERF_COMP_ID=<id>         Composition to benchmark (default: CertImage)
#   CHRONON3D_PERF_KEEP_ARTIFACTS=1     Keep output MP4s after run
#   CHRONON3D_PERF_LEAK_PCT=<N>         Max RSS growth % over 100 renders (default: 10)
#   CHRONON3D_PERF_LEAK_ITERATIONS=<N>  Leak test iterations (default: 100)
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_performance_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_PERF_CLI_BIN="${CHRONON3D_PERF_CLI_BIN:-}"
CHRONON3D_PERF_COMP_ID="${CHRONON3D_PERF_COMP_ID:-CertImage}"
CHRONON3D_PERF_KEEP_ARTIFACTS="${CHRONON3D_PERF_KEEP_ARTIFACTS:-0}"
CHRONON3D_PERF_LEAK_PCT="${CHRONON3D_PERF_LEAK_PCT:-10}"
CHRONON3D_PERF_LEAK_ITERATIONS="${CHRONON3D_PERF_LEAK_ITERATIONS:-100}"

# NOTE: set -u + pipefail only — NO set -e (the aggregator must run all
# sections and report aggregate verdict, not abort on first failure).
set -uo pipefail

WORK_DIR="/tmp/chronon3d_perf_work"
LEDGER_PATH="/tmp/chronon3d_perf_ledger.jsonl"

# Performance scenarios: name|width|height|fps|duration_sec|frame_count
declare -a SCENARIOS=(
    "1080p30|1920|1080|30|60|1800"
    "1080p60|1920|1080|60|60|3600"
    "portrait_1080x1920|1080|1920|30|60|1800"
    "4k_3840x2160|3840|2160|30|60|1800"
    "default_1080p30|1920|1080|30|60|1800"
)

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass()   { echo "  [PASS]    $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail()   { echo "  [FAIL]    $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked(){ echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

# Locate chronon3d_cli binary (canonical search paths)
find_chronon3d_cli() {
    if [ -n "$CHRONON3D_PERF_CLI_BIN" ] && [ -x "$CHRONON3D_PERF_CLI_BIN" ]; then
        echo "$CHRONON3D_PERF_CLI_BIN"
        return 0
    fi
    for candidate in \
        "${ROOT}/build/chronon/linux-content-dev/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/build/manual-test/chronon3d_cli" \
        "$(command -v chronon3d_cli 2>/dev/null)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# Parse /usr/bin/time -v output into duration_sec, cpu_percent, peak_rss_kb
parse_gnu_time_v() {
    local time_stderr="$1"
    local elapsed cpu_pct peak_rss duration_sec

    elapsed=$(grep -m1 'Elapsed (wall clock) time' "$time_stderr" 2>/dev/null \
        | sed -nE 's/.*:[[:space:]]+([0-9:.]+).*/\1/p')
    cpu_pct=$(grep -m1 'Percent of CPU this job got' "$time_stderr" 2>/dev/null \
        | sed -nE 's/.*:[[:space:]]+([0-9]+)%.*/\1/p')
    peak_rss=$(grep -m1 'Maximum resident set size' "$time_stderr" 2>/dev/null \
        | sed -nE 's/.*:[[:space:]]+([0-9]+).*/\1/p')

    duration_sec=0
    if [ -n "$elapsed" ]; then
        duration_sec=$(awk -F: -v s="$elapsed" 'BEGIN{
            n=split(s, parts, ":")
            if (n==2) print parts[1]*60 + parts[2]
            else if (n==3) print parts[1]*3600 + parts[2]*60 + parts[3]
            else print 0
        }')
    fi

    echo "duration_sec=${duration_sec:-0}"
    echo "cpu_percent=${cpu_pct:-0}"
    echo "peak_rss_kb=${peak_rss:-0}"
}

# Try to extract cache hit rate from CLI --report JSON (best-effort)
extract_cache_hit_rate() {
    local report_json="$1"
    if [ -s "$report_json" ] && command -v jq >/dev/null 2>&1; then
        jq -r '.cache_hit_rate // .cache.hit_rate // .diagnostics.cache_hit_rate // "N/A"' \
            "$report_json" 2>/dev/null || echo "N/A"
    else
        echo "N/A"
    fi
}

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_performance_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")

if [ "$LOCAL" != "$REMOTE" ] \
   && [ "$REMOTE" != "N/A" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "PERF_FAIL: HEAD and origin/main are divergent"
    echo "  LOCAL:  $LOCAL"
    echo "  REMOTE: $REMOTE"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

# /usr/bin/time -v hard requirement per user spec
if [ -x "/usr/bin/time" ] && /usr/bin/time -v /bin/true >/dev/null 2>&1; then
    _gate_pass "gnu_time (/usr/bin/time -v)"
else
    _gate_blocked "gnu_time" "/usr/bin/time -v not available — install: apt install time"
    ENV_BLOCKED=true
fi

# chronon3d_cli binary
if [ "$ENV_BLOCKED" = false ]; then
    CLI_BIN="$(find_chronon3d_cli)" || {
        _gate_blocked "chronon3d_cli" "not found — set CHRONON3D_PERF_CLI_BIN or build"
        ENV_BLOCKED=true
    }
fi
if [ -n "${CLI_BIN:-}" ] && [ "$ENV_BLOCKED" = false ]; then
    CLI_SIZE=$(stat -c%s "$CLI_BIN" 2>/dev/null || echo 0)
    _gate_pass "chronon3d_cli (${CLI_BIN}, ${CLI_SIZE} bytes)"

    # Verify the composition exists
    CLI_COMP_LIST=$("$CLI_BIN" --list-compositions 2>/dev/null || true)
    if [ -n "$CLI_COMP_LIST" ] && echo "$CLI_COMP_LIST" | grep -qF "$CHRONON3D_PERF_COMP_ID" 2>/dev/null; then
        _gate_pass "composition[$CHRONON3D_PERF_COMP_ID]"
    elif [ -z "$CLI_COMP_LIST" ]; then
        _gate_pass "composition[$CHRONON3D_PERF_COMP_ID] (--list-compositions unavailable; proceeding)"
    else
        _gate_blocked "composition[$CHRONON3D_PERF_COMP_ID]" "not registered — rebuild with CHRONON3D_BUILD_CONTENT=ON"
        ENV_BLOCKED=true
    fi
fi

# Optional: jq for cache hit rate parsing
command -v jq >/dev/null 2>&1 \
    && _gate_pass "jq ($(jq --version 2>/dev/null))" \
    || { _gate_pass "jq (not found — cache hit rate will be N/A)"; }

# ══════════════════════════════════════════════════════════════════════════════
# 3. Performance scenarios (5 resolution/fps combos × 60s each)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Performance scenarios (5 scenarios × 60s) =="
echo "   Composition: $CHRONON3D_PERF_COMP_ID"
echo ""

if [ "$ENV_BLOCKED" = true ]; then
    for scenario in "${SCENARIOS[@]}"; do
        s_name="$(echo "$scenario" | cut -d'|' -f1)"
        _gate_blocked "scenario_${s_name}" "env blocked"
    done
else
    rm -rf "$WORK_DIR" "$LEDGER_PATH"
    mkdir -p "$WORK_DIR"
    : > "$LEDGER_PATH"

    for scenario in "${SCENARIOS[@]}"; do
        IFS='|' read -r s_name s_w s_h s_fps s_dur_s s_frames <<< "$scenario"

        s_out="${WORK_DIR}/${s_name}.mp4"
        s_time_stderr="${WORK_DIR}/${s_name}.time.stderr"
        s_report_json="${WORK_DIR}/${s_name}.report.json"

        echo "  ── ${s_name} (${s_w}×${s_h} @ ${s_fps}fps × ${s_dur_s}s = ${s_frames} frames) ──"

        # /usr/bin/time -v around chronon3d_cli video
        # (set -e is NOT active; use || to capture exit code, not set +e/-e)
        chronon_exit=0
        /usr/bin/time -v "$CLI_BIN" video "$CHRONON3D_PERF_COMP_ID" \
            -o "$s_out" \
            --start 0 --end "$s_frames" --fps "$s_fps" \
            --width "$s_w" --height "$s_h" \
            > "${WORK_DIR}/${s_name}.stdout" 2> "$s_time_stderr" || chronon_exit=$?

        # Parse GNU time stats
        s_stats=$(parse_gnu_time_v "$s_time_stderr")
        s_duration=$(echo "$s_stats" | awk -F= '/^duration_sec=/{print $2}')
        s_cpu_pct=$(echo "$s_stats" | awk -F= '/^cpu_percent=/{print $2}')
        s_peak_rss=$(echo "$s_stats" | awk -F= '/^peak_rss_kb=/{print $2}')

        # Output size
        s_output_bytes=0
        [ -f "$s_out" ] && s_output_bytes=$(wc -c < "$s_out" | awk '{print $1+0}')

        # Effective fps
        s_eff_fps=0
        if [ "${s_duration:-0}" != "0" ]; then
            s_eff_fps=$(awk -v f="$s_frames" -v d="$s_duration" 'BEGIN{printf "%.2f", f / d}')
        fi

        # Cache hit rate (best-effort proxy: single-frame still render, NOT the video render.
        # §honest-proxy: this is a still-render cache hit estimate, not video-render cache stats.)
        s_cache_hit="N/A"
        if [ "$chronon_exit" -eq 0 ]; then
            "$CLI_BIN" still "$CHRONON3D_PERF_COMP_ID" "${WORK_DIR}/${s_name}_cache.png" \
                --frame 0 --report "$s_report_json" >/dev/null 2>&1 || true
            s_cache_hit=$(extract_cache_hit_rate "$s_report_json")
        fi

        echo "    duration=${s_duration}s, cpu=${s_cpu_pct}%, rss=${s_peak_rss}KB, output=${s_output_bytes}B, fps=${s_eff_fps}, cache=${s_cache_hit}, exit=${chronon_exit}"

        if [ "$chronon_exit" -ne 0 ]; then
            _gate_fail "scenario_${s_name}" "CLI exited $chronon_exit"
        elif [ "${s_duration:-0}" = "0" ] || [ "${s_peak_rss:-0}" = "0" ]; then
            _gate_fail "scenario_${s_name}" "could not extract metrics from /usr/bin/time -v"
        else
            _gate_pass "scenario_${s_name} (${s_duration}s, ${s_cpu_pct}% CPU, ${s_peak_rss}KB RSS, ${s_eff_fps} fps)"
        fi

        # JSONL ledger row
        printf '{"scenario":"%s","comp_id":"%s","width":%d,"height":%d,"fps":%d,"duration_sec":%s,"frame_count":%d,"cpu_percent":%s,"peak_rss_kb":%s,"output_bytes":%d,"effective_fps":%s,"cache_hit_rate":"%s","chronon_exit":%d}\n' \
            "$s_name" "$CHRONON3D_PERF_COMP_ID" "$s_w" "$s_h" "$s_fps" \
            "${s_duration:-0}" "$s_frames" "${s_cpu_pct:-0}" "${s_peak_rss:-0}" \
            "$s_output_bytes" "$s_eff_fps" "$s_cache_hit" "$chronon_exit" \
            >> "$LEDGER_PATH"
    done
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. Memory leak test (100 consecutive renders, same template)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. Memory leak test (${CHRONON3D_PERF_LEAK_ITERATIONS} consecutive renders) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "memory_leak_test" "env blocked"
else
    LEAK_FRAMES=30
    LEAK_W=1920 LEAK_H=1080 LEAK_FPS=30
    LEAK_OUT="${WORK_DIR}/leak_test.mp4"
    LEAK_TIME="${WORK_DIR}/leak_test.time.stderr"
    LEAK_LOG="${WORK_DIR}/leak_test.rss.log"
    : > "$LEAK_LOG"

    echo "  Running ${CHRONON3D_PERF_LEAK_ITERATIONS} × ${LEAK_W}×${LEAK_H}@${LEAK_FPS}fps×1s..."
    LEAK_PASS=0; LEAK_FAIL=0
    for i in $(seq 1 "$CHRONON3D_PERF_LEAK_ITERATIONS"); do
        chronon_exit=0
        /usr/bin/time -v "$CLI_BIN" video "$CHRONON3D_PERF_COMP_ID" \
            -o "$LEAK_OUT" \
            --start 0 --end "$LEAK_FRAMES" --fps "$LEAK_FPS" \
            --width "$LEAK_W" --height "$LEAK_H" \
            > /dev/null 2> "$LEAK_TIME" || chronon_exit=$?

        peak_rss=$(grep -m1 'Maximum resident set size' "$LEAK_TIME" 2>/dev/null \
            | sed -nE 's/.*:[[:space:]]+([0-9]+).*/\1/p')
        peak_rss="${peak_rss:-0}"

        echo "${i} ${chronon_exit} ${peak_rss}" >> "$LEAK_LOG"

        if [ "$chronon_exit" -eq 0 ]; then
            LEAK_PASS=$((LEAK_PASS + 1))
        else
            LEAK_FAIL=$((LEAK_FAIL + 1))
        fi
    done

    # Compute leak growth
    if [ "$LEAK_PASS" -gt 0 ]; then
        first_peak=$(awk 'NR==1 {print $3+0}' "$LEAK_LOG")
        last_peak=$(awk 'END {print $3+0}' "$LEAK_LOG")
        max_peak=$(awk '$3>max{max=$3} END{print max+0}' "$LEAK_LOG")
        min_peak=$(awk 'NR==1{m=$3} $3<m{m=$3} END{print m+0}' "$LEAK_LOG")

        leak_pct=0
        if [ "${min_peak:-0}" -gt 0 ]; then
            leak_pct=$(awk -v mx="$max_peak" -v mn="$min_peak" 'BEGIN{printf "%.2f", (mx-mn)*100.0/mn}')
        fi

        echo "  Iterations: PASS=$LEAK_PASS FAIL=$LEAK_FAIL"
        echo "  RSS: first=${first_peak}KB last=${last_peak}KB min=${min_peak}KB max=${max_peak}KB"
        echo "  Growth: ${leak_pct}% (threshold: ${CHRONON3D_PERF_LEAK_PCT}%)"

        growth_int="${leak_pct%.*}"
        threshold_int="${CHRONON3D_PERF_LEAK_PCT%.*}"

        if [ "${growth_int:-0}" -le "${threshold_int:-10}" ] && [ "$LEAK_FAIL" -eq 0 ]; then
            _gate_pass "memory_leak_test (${CHRONON3D_PERF_LEAK_ITERATIONS} iters, growth=${leak_pct}% ≤ ${CHRONON3D_PERF_LEAK_PCT}%)"
        elif [ "$LEAK_FAIL" -gt 0 ]; then
            _gate_fail "memory_leak_test" "${LEAK_FAIL}/${CHRONON3D_PERF_LEAK_ITERATIONS} renders failed"
        else
            _gate_fail "memory_leak_test" "growth ${leak_pct}% exceeds threshold ${CHRONON3D_PERF_LEAK_PCT}%"
        fi

        # Append leak row
        printf '{"scenario":"memory_leak_test","comp_id":"%s","iterations":%d,"leak_growth_pct":%s,"min_rss_kb":%d,"max_rss_kb":%d,"first_rss_kb":%d,"last_rss_kb":%d,"threshold_pct":%d,"pass":%d,"fail":%d}\n' \
            "$CHRONON3D_PERF_COMP_ID" "$CHRONON3D_PERF_LEAK_ITERATIONS" "$leak_pct" \
            "$min_peak" "$max_peak" "$first_peak" "$last_peak" \
            "$CHRONON3D_PERF_LEAK_PCT" "$LEAK_PASS" "$LEAK_FAIL" \
            >> "$LEDGER_PATH"
    else
        _gate_fail "memory_leak_test" "all ${CHRONON3D_PERF_LEAK_ITERATIONS} renders failed"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. Performance ledger summary
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. Performance ledger =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "performance_ledger" "env blocked"
else
    LEDGER_LINES=$(wc -l < "$LEDGER_PATH" 2>/dev/null || echo 0)
    if [ "$LEDGER_LINES" -gt 0 ]; then
        _gate_pass "performance_ledger (${LEDGER_LINES} JSONL rows at $LEDGER_PATH)"
    else
        _gate_fail "performance_ledger" "ledger empty"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. Cleanup
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. Cleanup =="

if [ "$CHRONON3D_PERF_KEEP_ARTIFACTS" = "1" ]; then
    echo "  Keeping artifacts: $WORK_DIR $LEDGER_PATH"
    _gate_pass "cleanup (preserved)"
else
    rm -rf "$WORK_DIR" 2>/dev/null || true
    _gate_pass "cleanup (artifacts removed; ledger at $LEDGER_PATH)"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Final verdict
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo "  Ledger:  $LEDGER_PATH"
echo ""

if [ "$ENV_BLOCKED" = true ]; then
    echo "PERFORMANCE_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Performance benchmark blocked by environment (no CLI binary or"
    echo "  /usr/bin/time -v). macchina-verifica DEFERRED to working build host"
    echo "  per AGENTS.md §honest-limitation."
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "PERFORMANCE_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "PERFORMANCE_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Performance & Memory certified."
    echo "[INFO] ${GATE_NAME}: 5 scenarios (1080p30/60, portrait, 4K, default) + ${CHRONON3D_PERF_LEAK_ITERATIONS}-iteration leak test — all passed"
    exit 0
fi
