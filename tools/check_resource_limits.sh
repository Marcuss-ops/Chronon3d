#!/usr/bin/env bash
# tools/check_resource_limits.sh — Test 13 — Limiti risorse CPU-first
#
# Closes TICKET-TEST-13-LIMITI-RISORSE.
#
# USER SPEC (verbatim): "definisi e gate-enforce peak RAM <512MB/job, nessun
# frame >2× P95, nessun leak tra 100 job, file temporanei=0 dopo completamento
# (valori da ricalibrare sulla macchina baseline 11/11). Integra i limiti nel
# gate canonico. Lavora su `main`, commit + push frequenti dopo ogni micro-green."
#
# 4 INVARIANTS (all machine-verified, all env-overridable for re-calibration):
#   (a) PEAK_RAM_BUDGET_MB    : max RSS across N_PEAK_RAM_RUNS jobs (via bash
#                              child PID VmHWM polling, 50ms granularity —
#                              pattern from check_linear_scaling.sh CRITICAL-1
#                              fix) ≤ 512MB (524,288 KB)
#   (b) FRAME_P95_MULTIPLIER  : for N_FRAME_P95_RUNS single-frame renders
#                              (canonical `ChrononGlowFinalAE --frame F`,
#                              F=0..N-1), max wall_time_per_frame ≤
#                              FRAME_P95_MULTIPLIER × P95(wall_times)
#   (c) LEAK_RUN_COUNT        : 100 sequential jobs; VmHWM@100 − VmHWM@1 ≤
#                              LEAK_THRESHOLD_KB (5 MB default); cache_after@100
#                              ≤ CACHE_GROWTH_BOUND × cache_after@1
#   (d) TEMP_FILE_WHITELIST   : enumerate $TMPDIR/chronon3d_* + /tmp/chronon3d_*
#                              + ~/.chronon3d (excluding the legitimate
#                              ~/.chronon3d/{cache,telemetry} runtime dirs)
#                              before vs after 1 test render; diff = 0
#
# All values HARDCODED with env-overridable defaults per AGENTS.md §honesty
# "valori da ricalibrare sulla macchina baseline 11/11" — the user recalibrates
# on the working build host (this VPS at HEAD has TICKET-CONTENT-TEXT-CAMERA-V1-ROT
# + TICKET-BUILD-ROT-CASCADE-CAMERA blocking the full `chronon3d_cli` compile
# per the established §13 honest-limitation pattern).
#
# Cat-3: pure tools/ artifact; zero new symbols in include/chronon3d/.
# Cat-5: 3-doc same-commit (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS).
#
# AGENTS.md INFO-level diagnostic style: emits ONE [INFO] ${GATE_NAME}: ...
# line on PASS addizionale al canonico GATE_PASS final line.

# ── Constants (env-overridable per the calibration rule) ───────────────
# Recalibration: set env var before invocation, e.g.
#   PEAK_RAM_BUDGET_MB=1024 bash tools/check_resource_limits.sh
#   FRAME_P95_MULTIPLIER=1.5 bash tools/check_resource_limits.sh
#   LEAK_THRESHOLD_KB=10240 bash tools/check_resource_limits.sh
# The defaults below are the canonical 11/11-baseline machine values; on a
# different baseline (e.g. 16GB-RAM host with 4K renders), the user raises
# the budget to the baseline machine's measured peak + safety margin.
GATE_NAME="check_resource_limits"
PEAK_RAM_BUDGET_MB="${PEAK_RAM_BUDGET_MB:-512}"            # hard upper bound per job (KB = 512*1024 = 524288)
PEAK_RAM_BUDGET_KB=$((PEAK_RAM_BUDGET_MB * 1024))          # for comparison
PEAK_RAM_RUNS="${PEAK_RAM_RUNS:-3}"                          # sample size for peak RAM (3 runs, take max)

FRAME_P95_MULTIPLIER="${FRAME_P95_MULTIPLIER:-2.0}"        # user-spec verbatim "2× P95"
FRAME_P95_RUNS="${FRAME_P95_RUNS:-20}"                      # 20 single-frame renders
FRAME_P95_POLLS="${FRAME_P95_POLLS:-20}"                    # sub-sample size for percentile calc

LEAK_RUN_COUNT="${LEAK_RUN_COUNT:-100}"                     # user-spec verbatim "tra 100 job"
LEAK_THRESHOLD_KB="${LEAK_THRESHOLD_KB:-5120}"              # 5 MB = 5120 KB absolute VmHWM growth
CACHE_GROWTH_BOUND="${CACHE_GROWTH_BOUND:-5}"               # cache_after@N ≤ 5× cache_after@1

TEMP_FILE_BUDGET="${TEMP_FILE_BUDGET:-0}"                   # user-spec verbatim "=0 dopo completamento"

# Composition + frame (consistent with Test 10 + Test 14 canonical)
COMPOSITION="${COMPOSITION:-ChrononGlowFinalAE}"
FRAME="${FRAME:-15}"

# CLI paths (env-overridable; per check_linear_scaling.sh pattern)
CLI_DEBUG_PATH="${CLI_DEBUG_PATH:-./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli}"
CLI_RELEASE_PATH="${CLI_RELEASE_PATH:-./build/chronon/linux-fast-release/apps/chronon3d_cli/chronon3d_cli}"

# Output directory (under $TMPDIR or /tmp per project convention)
OUT_DIR="$(mktemp -d -t chronon3d_resource_limits.XXXXXX)"
PEAK_TSV="${OUT_DIR}/peak_runs.tsv"
FRAME_TSV="${OUT_DIR}/frame_p95.tsv"
LEAK_TSV="${OUT_DIR}/leak_runs.tsv"

# Cleanup trap (per CRITICAL-2 + MINOR-E from check_linear_scaling.sh lineage)
trap 'rm -rf "$OUT_DIR" 2>/dev/null || true' EXIT

# ── Precondition check (per check_determinism.sh + check_linear_scaling.sh pattern) ──
# Internal-fail exit 2 if any precondition is unsatisfied (jq, du, mktemp, awk, CLI paths).
set -uo pipefail
precond_fail=0
for cmd in jq du mktemp awk xargs stat; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "GATE_FAIL_INTERNAL: required command '$cmd' not in PATH" >&2
        precond_fail=1
    fi
done
if [ ! -x "$CLI_DEBUG_PATH" ] && [ ! -x "$CLI_RELEASE_PATH" ]; then
    echo "GATE_FAIL_INTERNAL: neither CLI_DEBUG_PATH ('$CLI_DEBUG_PATH') nor CLI_RELEASE_PATH ('$CLI_RELEASE_PATH') is executable" >&2
    precond_fail=1
fi
if [ "$precond_fail" -ne 0 ]; then
    exit 2
fi
# Pick the available CLI (debug preferred; release fallback)
if [ -x "$CLI_DEBUG_PATH" ]; then
    CLI="$CLI_DEBUG_PATH"
else
    CLI="$CLI_RELEASE_PATH"
fi

# Helper: spawn one CLI render job + capture per-job VmHWM via bash child PID polling
# (reuses the CRITICAL-1 fix pattern from check_linear_scaling.sh: poll
# /proc/<cli_pid>/status VmHWM WHILE the cli is alive via 50ms loop, take MAX
# across iterations, then wait $cli_pid to capture exit code + close the loop).
# Args: $1=output_path, $2=frame_number, writes "wall_ns peak_rss_kb cache_after_bytes exit_code" to stdout
do_one_job() {
    local out_path="$1"
    local frame_num="$2"
    local start_ns end_ns wall_ns peak_rss_kb cur_rss cache_after_bytes ec cli_pid

    start_ns="$(date +%s%N)"

    set +e
    OMP_NUM_THREADS=1 \
    "$CLI" render "$COMPOSITION" \
        --frame "$frame_num" --output "$out_path" \
        >"${out_path}.stdout" 2>"${out_path}.stderr" &
    cli_pid=$!

    peak_rss_kb=0
    cur_rss="$(awk '/VmHWM/{print $2}' "/proc/$cli_pid/status" 2>/dev/null || echo 0)"
    peak_rss_kb="$cur_rss"
    while kill -0 "$cli_pid" 2>/dev/null; do
        cur_rss="$(awk '/VmHWM/{print $2}' "/proc/$cli_pid/status" 2>/dev/null || echo 0)"
        if [ "${cur_rss:-0}" -gt "${peak_rss_kb:-0}" ]; then
            peak_rss_kb="$cur_rss"
        fi
        sleep 0.05 2>/dev/null || sleep 1
    done
    wait "$cli_pid" 2>/dev/null
    ec=$?
    set -e
    end_ns="$(date +%s%N)"
    wall_ns=$((end_ns - start_ns))

    cache_after_bytes="$(du -sb "$HOME/.chronon3d/cache/" 2>/dev/null | awk '{print $1}' || echo 0)"

    echo "$wall_ns $peak_rss_kb $cache_after_bytes $ec"
}

# ── PHASE 1: PEAK RAM < 512MB/job ───────────────────────────────────────
echo "=== PHASE 1: PEAK RAM < ${PEAK_RAM_BUDGET_MB}MB/job (${PEAK_RAM_RUNS} runs) ==="
peak_max_kb=0
peak_violations=0
peak_max_kb=0
for i in $(seq 1 "$PEAK_RAM_RUNS"); do
    job_out="${OUT_DIR}/peak_${i}.mp4"
    read -r wall peak cache ec < <(do_one_job "$job_out" "$FRAME")
    echo -e "PEAK\t${i}\t${peak}\t${wall}\t${ec}" >> "$PEAK_TSV"
    if [ "${peak:-0}" -gt "${peak_max_kb:-0}" ]; then
        peak_max_kb="$peak"
    fi
done
peak_pass=0
if [ "${peak_max_kb:-0}" -le "${PEAK_RAM_BUDGET_KB}" ]; then
    peak_pass=1
fi
echo "  peak_max_kb = ${peak_max_kb} / budget_kb = ${PEAK_RAM_BUDGET_KB} (${PEAK_RAM_BUDGET_MB}MB)"

# ── PHASE 2: FRAME P95 (no frame > 2× P95) ──────────────────────────────
echo "=== PHASE 2: NO FRAME > ${FRAME_P95_MULTIPLIER}× P95 (${FRAME_P95_RUNS} frames) ==="
frame_max_wall_ns=0
for f in $(seq 0 $((FRAME_P95_RUNS - 1))); do
    job_out="${OUT_DIR}/frame_${f}.mp4"
    read -r wall peak cache ec < <(do_one_job "$job_out" "$f")
    echo -e "FRAME\t${f}\t${wall}\t${peak}\t${ec}" >> "$FRAME_TSV"
    if [ "${wall:-0}" -gt "${frame_max_wall_ns:-0}" ]; then
        frame_max_wall_ns="$wall"
    fi
done
# Compute P95 via awk (no sort/awk-pipe complexity; awk sorts in-memory for small N)
frame_p95_ns="$(awk -v n="$FRAME_P95_RUNS" '
    { w[NR]=$2 }
    END {
        # Simple in-place insertion sort on w[]
        for (i=2; i<=NR; i++) {
            k = w[i]; j = i-1
            while (j>=1 && w[j] > k) { w[j+1] = w[j]; j-- }
            w[j+1] = k
        }
        # P95 = w[ceil(0.95*NR)] (1-indexed)
        idx = int(0.95 * NR + 0.999999)
        if (idx < 1) idx = 1
        if (idx > NR) idx = NR
        print w[idx]
    }
' "$FRAME_TSV")"
# frame_max must be ≤ multiplier × P95
# use awk for the multiply to avoid bash int overflow on ns values
frame_threshold_ns="$(awk -v m="$frame_max_wall_ns" -v k="$FRAME_P95_MULTIPLIER" 'BEGIN{printf "%d", m*k}')"
frame_p95_pass=0
if [ "${frame_max_wall_ns:-0}" -le "${frame_p95_ns:-0}" ] || [ "${frame_max_wall_ns:-0}" -le "${frame_threshold_ns:-0}" ]; then
    frame_p95_pass=1
fi
# Tighter check: max ≤ multiplier × P95 (the literal user spec)
frame_p95_max_ratio="$(awk -v m="$frame_max_wall_ns" -v p="$frame_p95_ns" 'BEGIN{if (p>0) printf "%.3f", m/p; else printf "9999"}')"
frame_p95_violation=0
# Compare max / P95 against multiplier
frame_p95_ratio_pass="$(awk -v r="$frame_p95_max_ratio" -v k="$FRAME_P95_MULTIPLIER" 'BEGIN{print (r<=k+0.0001) ? 1 : 0}')"
if [ "$frame_p95_ratio_pass" -ne 1 ]; then
    frame_p95_violation=1
fi
echo "  frame_max_wall_ns = ${frame_max_wall_ns}  P95_ns = ${frame_p95_ns}  ratio = ${frame_p95_max_ratio}×  budget = ${FRAME_P95_MULTIPLIER}×"

# ── PHASE 3: NO LEAK TRA 100 JOB (VmHWM stability + cache bounded) ──────
echo "=== PHASE 3: NO LEAK TRA ${LEAK_RUN_COUNT} JOB ==="
leak_first_kb=0
leak_last_kb=0
leak_delta_kb=0
leak_first_cache=0
leak_last_cache=0
leak_cache_ratio=0
leak_violation=0
leak_error_count=0
# Cold-wipe cache BEFORE the leak run (per check_linear_scaling.sh pattern)
rm -rf "$HOME/.chronon3d/cache/" 2>/dev/null || true
mkdir -p "$HOME/.chronon3d/cache/" 2>/dev/null || true
for i in $(seq 1 "$LEAK_RUN_COUNT"); do
    job_out="${OUT_DIR}/leak_${i}.mp4"
    read -r wall peak cache ec < <(do_one_job "$job_out" "$FRAME")
    echo -e "LEAK\t${i}\t${peak}\t${cache}\t${ec}" >> "$LEAK_TSV"
    if [ "$i" -eq 1 ]; then
        leak_first_kb="$peak"
        leak_first_cache="$cache"
    fi
    leak_last_kb="$peak"
    leak_last_cache="$cache"
    if [ "${ec:-0}" -ne 0 ]; then
        leak_error_count=$((leak_error_count + 1))
    fi
done
leak_delta_kb=$((leak_last_kb - leak_first_kb))
# absolute value (in case VmHWM dropped)
[ "${leak_delta_kb:-0}" -lt 0 ] && leak_delta_kb=$((-leak_delta_kb))
leak_cache_ratio="$(awk -v a="$leak_last_cache" -v b="$leak_first_cache" 'BEGIN{if (b>0) printf "%.3f", a/b; else printf "9999"}')"
leak_pass=1
if [ "${leak_delta_kb:-0}" -gt "${LEAK_THRESHOLD_KB}" ]; then
    leak_pass=0
fi
# cache ratio via awk (avoid integer division)
leak_cache_ratio_pass="$(awk -v r="$leak_cache_ratio" -v k="$CACHE_GROWTH_BOUND" 'BEGIN{print (r<=k+0.0001) ? 1 : 0}')"
if [ "$leak_cache_ratio_pass" -ne 1 ]; then
    leak_pass=0
fi
# error rate ≤ 0 for a clean run (any error = leak signal)
if [ "$leak_error_count" -gt 0 ]; then
    leak_pass=0
fi
echo "  VmHWM@1 = ${leak_first_kb} KB  VmHWM@${LEAK_RUN_COUNT} = ${leak_last_kb} KB  delta = ${leak_delta_kb} KB (threshold ${LEAK_THRESHOLD_KB})"
echo "  cache@1 = ${leak_first_cache} B  cache@${LEAK_RUN_COUNT} = ${leak_last_cache} B  ratio = ${leak_cache_ratio}× (bound ${CACHE_GROWTH_BOUND}×)  errors = ${leak_error_count}"

# ── PHASE 4: TEMP FILES = 0 DOPO COMPLETAMENTO ───────────────────────────
echo "=== PHASE 4: TEMP FILES = ${TEMP_FILE_BUDGET} DOPO COMPLETAMENTO ==="
# Whitelist: ~/.chronon3d/cache + ~/.chronon3d/telemetry are LEGITIMATE runtime
# dirs. Anything under $TMPDIR/chronon3d_* or /tmp/chronon3d_* or ~/.chronon3d/*
# outside the whitelist is a SUSPECT temp-file creation.
WHITELIST_REGEX="^${HOME}/.chronon3d/(cache|telemetry)/"
temp_before_count="$(find "$TMPDIR" /tmp "$HOME/.chronon3d" -maxdepth 4 -user "$(id -u)" \( -name 'chronon3d_*' -o -path "$HOME/.chronon3d/*" \) 2>/dev/null | grep -vE "$WHITELIST_REGEX" | wc -l | awk '{print $1}')"
# Run 1 test render to give the CLI a chance to leave temp files
job_out="${OUT_DIR}/temp_probe.mp4"
do_one_job "$job_out" "$FRAME" >/dev/null
# Allow a brief settle window for any async cleanup
sleep 1
temp_after_count="$(find "$TMPDIR" /tmp "$HOME/.chronon3d" -maxdepth 4 -user "$(id -u)" \( -name 'chronon3d_*' -o -path "$HOME/.chronon3d/*" \) 2>/dev/null | grep -vE "$WHITELIST_REGEX" | wc -l | awk '{print $1}')"
temp_diff=$((temp_after_count - temp_before_count))
temp_pass=1
if [ "$temp_diff" -gt "$TEMP_FILE_BUDGET" ]; then
    temp_pass=0
fi
echo "  temp_files before = ${temp_before_count}  after = ${temp_after_count}  diff = ${temp_diff}  budget = ${TEMP_FILE_BUDGET}"

# ── Aggregate verdict + per-invariant diagnostics ──────────────────────
echo ""
echo "=== AGGREGATE VERDICT ==="
overall_pass=1
if [ "$peak_pass" -ne 1 ]; then
    echo "GATE_FAIL: PEAK RAM ${peak_max_kb} KB exceeds budget ${PEAK_RAM_BUDGET_KB} KB (${PEAK_RAM_BUDGET_MB}MB)"
    overall_pass=0
fi
if [ "$frame_p95_violation" -ne 0 ]; then
    echo "GATE_FAIL: FRAME P95 ratio ${frame_p95_max_ratio}× exceeds budget ${FRAME_P95_MULTIPLIER}× (max=${frame_max_wall_ns}ns, P95=${frame_p95_ns}ns)"
    overall_pass=0
fi
if [ "$leak_pass" -ne 1 ]; then
    echo "GATE_FAIL: LEAK delta=${leak_delta_kb}KB > ${LEAK_THRESHOLD_KB}KB OR cache_ratio=${leak_cache_ratio}× > ${CACHE_GROWTH_BOUND}× OR errors=${leak_error_count}"
    overall_pass=0
fi
if [ "$temp_pass" -ne 1 ]; then
    echo "GATE_FAIL: TEMP FILES diff=${temp_diff} > budget ${TEMP_FILE_BUDGET}"
    overall_pass=0
fi

if [ "$overall_pass" -eq 1 ]; then
    echo "GATE_PASS: 4/4 invariants — peak=${peak_max_kb}KB/${PEAK_RAM_BUDGET_MB}MB + frame_P95=${frame_p95_max_ratio}×/${FRAME_P95_MULTIPLIER}× + leak_delta=${leak_delta_kb}KB/${LEAK_THRESHOLD_KB}KB + temp_diff=${temp_diff}/${TEMP_FILE_BUDGET}"
    echo "[INFO] ${GATE_NAME}: 4/4 invariants within budgets on $(uname -srm | tr ' ' '_') (re-calibrate constants on the 11/11 baseline machine per spec)"
    exit 0
else
    echo "GATE_FAIL: 1+ of 4 invariants violated (see per-phase diagnostic above)"
    exit 1
fi
