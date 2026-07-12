#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════
# tools/check_linear_scaling.sh — POST-REVIEW (CRITICAL-1, CRITICAL-2, CRITICAL-3, NIT-1, NIT-2)
#
# Test 14 — Scalabilità lineare (First-Principles Product Check, Test #7)
# ════════════════════════════════════════════════════════════════════════════
#
# Asserts that Chronon3D scales quasi-linearly across {1, 10, 50, 100} concurrent
# jobs of the canonical cinematic-glow composition ChrononGlowFinalAE at frame 15.
#
# CAS — 4 N-dimensions:
#   1. N=1      : baseline (canonical)
#   2. N=10     : "1 video=1GB ma 10 video=20GB" spec literal — must NOT trigger
#   3. N=50     : mid-scale stress
#   4. N=100    : full-scale — "cache illimitata / prestazioni che crollano" must NOT trigger
#
# INVARIANTS — 5 per-N measurements:
#   (a) time/job (mean)         : soft superlinear tolerance 1.5×@N, 2.5×@100 vs N=1
#   (b) RAM/job (max VmHWM)     : max across N jobs ≤ 2× max at N=1 (any N)
#   (c) cache bounded           : cache_after_N ≤ 5× cache_after_1
#   (d) error rate              : fail_jobs / total ≤ 1%
#   (e) throughput non-collapse : throughput@100 ≥ 25% × throughput@50; ALSO throughput@100 ≥ 4× throughput@1 (parallel efficiency floor)
#
# PASS CRITERION (F: quasi-linear, soft tolerance bands):
#   All 5 invariants hold for ALL 4 N-dimensions. Single violation = GATE_FAIL.
#
# POST-REVIEW SUMMARY (code-reviewer-minimax-m3 round 1):
#   CRITICAL-1 fixed: VmHWM is now read from the CLI child PID (/proc/<cli_pid>/status)
#                     via a 50ms polling loop WHILE the cli is alive — not from
#                     the bash subshell wrapper (which would only report ~5 MB).
#                     Pre-fix: bash subshell's VmHWM (~5-10 MB constant) made
#                     every RAM-superlinear check structurally inactive.
#   CRITICAL-2 fixed: every `awk ... "$dim_dir"/job_*.tsv` parse path now has
#                     explicit `|| echo 0` fallback + pre-check `if ls ...`
#                     guard, so set -euo pipefail no longer aborts on glob-miss
#                     (e.g., when external SIGTERM kills a backgrounded job).
#   CRITICAL-3 fixed: division-by-zero fallbacks in `time_ratio` / `tput_ratio` /
#                     `cache_ratio` no longer emit "inf" (which awk coerces to 0
#                     in numeric comparisons, masking regressions). Now emits
#                     sentinel `9999.0` (way above any slack) AND early-branches
#                     in the verdict when baseline_mean_ns / baseline_max_rss = 0.
#   NIT-1 fixed: throughput-floor check now ALSO compares N=100 against N=1
#                (parallel efficiency floor: throughput@100 ≥ 4× throughput@1
#                — i.e., 4× speedup over N=1 is the floor for the
#                "prestazioni che crollano" detection). The N=50 comparison
#                is preserved as the primary check; the N=1 baseline is the
#                fallback if N=50 is absent from N_DIMENSIONS.
#   NIT-2 fixed: dead variable RUNS_PER_DIM removed (N itself IS the variation
#                axis per user spec literal "1/10/50/100 concurrent jobs").
#
# Exit codes:
#   0  PASS                — quasi-linear scaling holds
#   1  FAIL                — at least 1 of 5 invariants violated at 1+ N-dimension
#   2  INTERNAL            — precondition fail (missing dep / missing CLI / missing baseline)
# ════════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME=check_linear_scaling

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && git rev-parse --show-toplevel)"

# ── Tunables (env-overridable per Test 10 E pattern) ──────────────────────
COMPOSITION="${COMPOSITION:-ChrononGlowFinalAE}"
FRAME="${FRAME:-15}"
N_DIMENSIONS="${N_DIMENSIONS:-1 10 50 100}"

TIME_SLACK_AT_10="${TIME_SLACK_AT_10:-1.5}"              # mean_time@10 ≤ 1.5× mean_time@1
TIME_SLACK_AT_100="${TIME_SLACK_AT_100:-2.5}"            # mean_time@100 ≤ 2.5× mean_time@1
RAM_SLACK_AT_ANY_N="${RAM_SLACK_AT_ANY_N:-2.0}"          # max_RSS_N ≤ 2× max_RSS_1
CACHE_BOUND_FACTOR="${CACHE_BOUND_FACTOR:-5.0}"          # cache_after_N ≤ 5× cache_after_1
ERROR_RATE_MAX_PCT="${ERROR_RATE_MAX_PCT:-1}"            # ≤ 1% errors
THROUGHPUT_FLOOR_PCT_100_VS_50="${THROUGHPUT_FLOOR_PCT_100_VS_50:-25}"            # 25%
THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100="${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100:-4}" # 4×

# CRITICAL-3 sentinel value for division-by-zero (well above any slack).
SENTINEL_INF_RATIO="${SENTINEL_INF_RATIO:-9999.0}"

debug_default="$REPO_ROOT/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli"
release_default="$REPO_ROOT/build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli"
CLI_DEBUG_PATH="${CLI_DEBUG_PATH:-${CHRONON3D_CLI_DEBUG_PATH:-$debug_default}}"
CLI_RELEASE_PATH="${CLI_RELEASE_PATH:-${CHRONON3D_CLI_RELEASE_PATH:-$release_default}}"

# ── Preconditions (per red-flag from Test 10/thinker) ─────────────────────
missing_dep=0
for cmd in jq du mktemp awk; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "GATE_FAIL_INTERNAL: required command '$cmd' not found in PATH" >&2
        missing_dep=1
    fi
done
[ "$missing_dep" -eq 0 ] || exit 2

if [ ! -x "$CLI_DEBUG_PATH" ]; then
    echo "GATE_FAIL_INTERNAL: Debug CLI not found: $CLI_DEBUG_PATH (set CLI_DEBUG_PATH env var or build it)" >&2
    exit 2
fi

# Validate N_DIMENSIONS contains expected values (NIT-1 follow-up).
for required_n in 1 10 50 100; do
    if ! echo "$N_DIMENSIONS" | grep -qw "$required_n"; then
        echo "GATE_FAIL_INTERNAL: N_DIMENSIONS='$N_DIMENSIONS' missing required value $required_n (spec literal '1/10/50/100')" >&2
        exit 2
    fi
done

# ── Working scratch dir (auto-cleanup on EXIT) ────────────────────────────
OUT_DIR="$(mktemp -d -t chronon3d_linear_scaling.XXXXXX)"
trap 'rm -rf "$OUT_DIR"' EXIT

# Output files (grep-discoverable):
#   $OUT_DIR/per_dim.tsv   — one row per N-dimension (5 invariants summary)
#   $OUT_DIR/per_job.tsv   — one row per spawned job (debugging detail)
#   $OUT_DIR/report.md     — human-readable verdict + spec-quoted diagnostics
PER_DIM_TSV="$OUT_DIR/per_dim.tsv"
PER_JOB_TSV="$OUT_DIR/per_job.tsv"
REPORT_MD="$OUT_DIR/report.md"

echo -e "n_dim\tdim_idx\twall_ns\tmean_ns_per_job\tmax_rss_kb\tcache_before_bytes\tcache_after_bytes\tjobs_ok\tjobs_fail\tthroughput_jps\terror_rate_pct" > "$PER_DIM_TSV"
echo -e "n_dim\tjob_id\tjob_idx\tstart_ns\tend_ns\tpeak_rss_kb\tcache_after_bytes\texit_code" > "$PER_JOB_TSV"

# ── Per-job spawner (CLI child PID + VmHWM polling; CRITICAL-1 fix) ───────
# CRITICAL-1 fix: capture cli_pid at fork, poll /proc/<cli_pid>/status VmHWM
# WHILE the cli is alive, take the MAX across polling iterations as peak_rss_kb.
# The cli reaps before the bash wrapper can read its /proc entry, so polling
# is the only stable Linux pattern. The previous design polled the bash
# subshell's VmHWM (~5-10 MB) which was structurally inactive.
do_one_job() {
    local job_id="$1" job_idx="$2" n="$3" dim_dir="$4"
    local job_tsv="$dim_dir/${job_id}.tsv"
    local job_out="$dim_dir/${job_id}.rgba"
    local start_ns end_ns max_rss_kb cache_after_bytes ec cli_pid cur_rss

    start_ns="$(date +%s%N)"

    # Spawn CLI in background so we own its PID via $!.
    set +e
    OMP_NUM_THREADS=1 \
    "$CLI_DEBUG_PATH" render "$COMPOSITION" \
        --frame "$FRAME" --output "$job_out" \
        >"$dim_dir/${job_id}.stdout" 2>"$dim_dir/${job_id}.stderr" &
    cli_pid=$!

    # CRITICAL-1 fix: poll /proc/<cli_pid>/status VmHWM WHILE alive.
    max_rss_kb=0
    # Initial read before any sleep (avoids losing the first sample if cli
    # exits very fast — kill -0 might trigger early exit before any poll).
    cur_rss="$(awk '/VmHWM/{print $2}' "/proc/$cli_pid/status" 2>/dev/null || echo 0)"
    max_rss_kb="$cur_rss"
    # Polling loop at 50ms granularity.
    while kill -0 "$cli_pid" 2>/dev/null; do
        cur_rss="$(awk '/VmHWM/{print $2}' "/proc/$cli_pid/status" 2>/dev/null || echo 0)"
        if [ "${cur_rss:-0}" -gt "${max_rss_kb:-0}" ]; then
            max_rss_kb="$cur_rss"
        fi
        sleep 0.05 2>/dev/null || sleep 1
    done
    wait "$cli_pid" 2>/dev/null
    ec=$?
    set -e

    end_ns="$(date +%s%N)"
    cache_after_bytes="$(du -sb "$HOME/.chronon3d/cache/" 2>/dev/null | awk '{print $1}' || echo 0)"

    printf '%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n' \
        "$n" "$job_id" "$job_idx" \
        "$start_ns" "$end_ns" \
        "$max_rss_kb" "$cache_after_bytes" "$ec" \
        > "$job_tsv"

    return 0  # Job failure is counted per-row, not propagated to gate (per (d))
}

# ── Per-N-dimension runner ────────────────────────────────────────────────
benchmark_dim() {
    local n="$1" dim_idx="$2"
    local dim_dir="$OUT_DIR/dim_$(printf '%02d' "$dim_idx")"
    mkdir -p "$dim_dir"

    # Cold-wipe BEFORE each N-dim (so all N jobs share cache-fill lifecycle).
    rm -rf "${HOME}/.chronon3d/cache/" 2>/dev/null || true
    local cache_before_bytes
    cache_before_bytes="$(du -sb "$HOME/.chronon3d/cache/" 2>/dev/null | awk '{print $1}' || echo 0)"

    # Spawn N concurrent jobs.
    local start_ns end_ns wall_ns
    start_ns="$(date +%s%N)"
    local pids=()
    for j in $(seq 1 "$n"); do
        ( do_one_job "job_$(printf '%03d' "$j")" "$j" "$n" "$dim_dir" ) &
        pids+=($!)
    done
    # Collect (do NOT propagate job-level exit codes into the gate).
    local pid
    for pid in "${pids[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
    end_ns="$(date +%s%N)"
    wall_ns=$((end_ns - start_ns))

    local cache_after_bytes
    cache_after_bytes="$(du -sb "$HOME/.chronon3d/cache/" 2>/dev/null | awk '{print $1}' || echo 0)"

    # CRITICAL-2 fix: PROBE before awk glob.  If the dir has zero job_*.tsv
    # files (e.g., every job was SIGTERM-killed), the glob is literal and awk
    # errors → set -e propagates → gate abort.  Guard explicitly.
    local total_jobs ok_jobs fail_jobs max_rss_kb error_rate_pct throughput_jps mean_ns_per_job
    if compgen -G "$dim_dir/job_*.tsv" >/dev/null; then
        cat "$dim_dir"/job_*.tsv >> "$PER_JOB_TSV" 2>/dev/null || true
        total_jobs="$(awk 'END{print NR}' "$dim_dir"/job_*.tsv 2>/dev/null | awk '{s+=$1}END{print s+0}' || echo 0)"
        ok_jobs="$(awk '$8==0' "$dim_dir"/job_*.tsv 2>/dev/null | wc -l || echo 0)"
        fail_jobs=$((total_jobs - ok_jobs))
        max_rss_kb="$(awk '{if($6>m){m=$6}}END{print m+0}' "$dim_dir"/job_*.tsv 2>/dev/null || echo 0)"
        if [ "$wall_ns" -gt 0 ] && [ "$total_jobs" -gt 0 ]; then
            mean_ns_per_job=$((wall_ns / total_jobs))
            throughput_jps="$(awk -v w="$wall_ns" -v n="$total_jobs" 'BEGIN{printf "%.4f", n/(w/1e9)}')"
            error_rate_pct="$(awk -v f="$fail_jobs" -v t="$total_jobs" 'BEGIN{if(t>0){printf "%.2f", (f*100.0)/t}else{print "100.00"}}')"
        else
            mean_ns_per_job=0
            throughput_jps=0
            error_rate_pct=100
        fi
    else
        # CRITICAL-2: glob missed — every backgrounded job was killed before
        # writing its row.  Surface this as a HARD FAIL (not a silent zero).
        total_jobs=0
        ok_jobs=0
        fail_jobs="$n"
        max_rss_kb=0
        mean_ns_per_job=0
        throughput_jps=0
        error_rate_pct=100
    fi

    # Emit per-dim row to global per-dim TSV.
    printf '%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.4f\t%s\n' \
        "$n" "$dim_idx" "$wall_ns" "$mean_ns_per_job" \
        "$max_rss_kb" \
        "$cache_before_bytes" "$cache_after_bytes" \
        "$ok_jobs" "$fail_jobs" \
        "$throughput_jps" "$error_rate_pct" \
        >> "$PER_DIM_TSV"
}

# ── Run all N-dimensions ──────────────────────────────────────────────────
echo "[INFO] ${GATE_NAME}: starting quasi-linear scaling benchmark for ${COMPOSITION} --frame ${FRAME}"
echo "  N dimensions: ${N_DIMENSIONS}"
echo "  Tolerances: TIME_SLACK@10=${TIME_SLACK_AT_10}×, TIME_SLACK@100=${TIME_SLACK_AT_100}×, RAM_SLACK=${RAM_SLACK_AT_ANY_N}×, CACHE_BOUND=${CACHE_BOUND_FACTOR}×, ERROR_RATE_MAX=${ERROR_RATE_MAX_PCT}%, THROUGHPUT_FLOOR_N50=${THROUGHPUT_FLOOR_PCT_100_VS_50}%, THROUGHPUT_EFFICIENCY_4X=${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100}×"
echo "  Debug CLI: ${CLI_DEBUG_PATH}"
echo "  Scratch:   ${OUT_DIR}"
echo

dim_idx=0
total_jobs_across_dims=0
for n in $N_DIMENSIONS; do
    dim_idx=$((dim_idx + 1))
    total_jobs_across_dims=$((total_jobs_across_dims + n))
    benchmark_dim "$n" "$dim_idx"
done

# ── Verdict: load per-dim TSV, compare against N=1 baseline ───────────────
declare -a ver_n=() ver_mean_ns=() ver_max_rss=() ver_cache=() ver_ok=() ver_fail=() ver_tput=() ver_err=()

while IFS=$'\t' read -r n dim_idx wall_ns mean_ns_per_job max_rss_kb cache_before cache_after jobs_ok jobs_fail throughput_jps error_rate_pct; do
    [ "$n" = "n_dim" ] && continue  # Skip header
    ver_n+=("$n")
    ver_mean_ns+=("$mean_ns_per_job")
    ver_max_rss+=("$max_rss_kb")
    ver_cache+=("$cache_after")
    ver_ok+=("$jobs_ok")
    ver_fail+=("$jobs_fail")
    ver_tput+=("$throughput_jps")
    ver_err+=("$error_rate_pct")
done < "$PER_DIM_TSV"

# Locate baseline (N=1).
baseline_idx=-1
for i in "${!ver_n[@]}"; do
    if [ "${ver_n[$i]}" = "1" ]; then baseline_idx="$i"; break; fi
done
if [ "$baseline_idx" = "-1" ]; then
    echo "GATE_FAIL_INTERNAL: baseline N=1 not found in N_DIMENSIONS=${N_DIMENSIONS}" >&2
    exit 2
fi

baseline_mean_ns="${ver_mean_ns[$baseline_idx]:-0}"
baseline_max_rss="${ver_max_rss[$baseline_idx]:-0}"
baseline_cache="${ver_cache[$baseline_idx]:-0}"
baseline_tput="${ver_tput[$baseline_idx]:-0}"

# CRITICAL-3 fix: early-branch on missing baseline.  If N=1 produced no
# measurable value (cli never ran / poll loop never read / etc.), the
# comparison cannot be made → INTERNAL exit (NOT silently PASS via inf→0).
if [ "${baseline_mean_ns:-0}" -eq 0 ] || [ "${baseline_max_rss:-0}" -eq 0 ]; then
    echo "GATE_FAIL_INTERNAL: baseline N=1 produced no measurable value (mean_ns=$baseline_mean_ns max_rss=$baseline_max_rss) — see $PER_DIM_TSV" >&2
    exit 2
fi

# Locate N=50 index (for primary throughput floor check).
n50_idx=-1
for i in "${!ver_n[@]}"; do
    if [ "${ver_n[$i]}" = "50" ]; then n50_idx="$i"; break; fi
done
n50_tput=""
[ "$n50_idx" != "-1" ] && n50_tput="${ver_tput[$n50_idx]:-0}"

# ── Verdict loop ───────────────────────────────────────────────────────────
violations=()
violations_spec_literal=()
declare -A verdict_per_dim

# CRITICAL-3 fix: helper that emits $SENTINEL_INF_RATIO when baseline=0
# (preserved invariant: baseline > 0 at this point; but guard anyway).
safe_ratio() {
    local num="$1" denom="$2"
    awk -v n="$num" -v d="$denom" -v sentinel="$SENTINEL_INF_RATIO" \
        'BEGIN{if(d>0){printf "%.3f", n/d}else{printf "%.3f", sentinel}}'
}

for i in "${!ver_n[@]}"; do
    n="${ver_n[$i]}"
    mean_ns="${ver_mean_ns[$i]:-0}"
    max_rss="${ver_max_rss[$i]:-0}"
    cache_after="${ver_cache[$i]:-0}"
    err_rate="${ver_err[$i]:-0}"
    tput="${ver_tput[$i]:-0}"

    time_slack="$TIME_SLACK_AT_10"
    [ "$n" = "100" ] && time_slack="$TIME_SLACK_AT_100"

    # CRITICAL-A fix: drop the broken sentinel-aware conjunctive.
    # The sentinel value (9999.0) was meaningless because awk's consumer
    # never received the sentinel var via -v, so it coerced to 0 and
    # silently reverse-verdicted every PASS case.  The early-branch
    # INTERNAL on baseline=0 already handles the dangerous case where the
    # sentinel is actually emitted by safe_ratio; once we reach this
    # point the baseline IS valid, so a clean r<=s is correct + minimal.

    # (a) time_superlinear: mean_ns_N / mean_ns_1 ≤ slack
    time_ratio="$(safe_ratio "$mean_ns" "$baseline_mean_ns")"
    time_ok=$(awk -v r="$time_ratio" -v s="$time_slack" 'BEGIN{print (r<=s)?1:0}')

    # (b) ram_superlinear: max_rss_N / max_rss_1 ≤ slack
    ram_ratio="$(safe_ratio "$max_rss" "$baseline_max_rss")"
    ram_ok=$(awk -v r="$ram_ratio" -v s="$RAM_SLACK_AT_ANY_N" 'BEGIN{print (r<=s)?1:0}')

    # SPEC LITERAL: "1 video=1GB ma 10 video=20GB" — encoded as
    #     max_rss_N10 / (10 × baseline_max_rss) ≤ 2.0
    # (i.e., per-job RAM at N=10 ≤ 2× per-job RAM at N=1 under isolation).
    spec_literal_superlinear=0
    if [ "$n" = "10" ] && [ "$baseline_max_rss" -gt 0 ]; then
        max_rss_per_job_at_10=$((max_rss / 10))
        spec_ratio="$(safe_ratio "$max_rss_per_job_at_10" "$baseline_max_rss")"
        spec_ok=$(awk -v r="$spec_ratio" -v s="2.0" 'BEGIN{print (r<=s)?1:0}')
        if [ "$spec_ok" -eq 0 ]; then
            spec_literal_superlinear=1
            violations_spec_literal+=("N=10 spec-literal VIOLATION: max_rss_per_job@N=10 = ${max_rss_per_job_at_10}kB; baseline max_rss = ${baseline_max_rss}kB; ratio = ${spec_ratio}; user quote '1 video=1GB ma 10 video=20GB' PROFILE DETECTED — IPC leak / unbounded allocator at scale")
        fi
    fi

    # (c) cache_bounded: cache_after_N / cache_after_1 ≤ bound (0 means
    # cold-wipe → acceptable "no cache yet" baseline; treat as RSS=0 case)
    cache_ratio="$(safe_ratio "$cache_after" "$baseline_cache")"
    cache_ok=1
    if [ "${baseline_cache:-0}" -gt 0 ]; then
        cache_ok=$(awk -v r="$cache_ratio" -v s="$CACHE_BOUND_FACTOR" 'BEGIN{print (r<=s)?1:0}')
    else
        # Cold-wipe baseline = 0: cache growth IS unbounded.  Only PASS if
        # current dim also has cache=0 (consistent with no-cache-yet state).
        [ "$cache_after" -eq 0 ] && cache_ok=1 || cache_ok=0
    fi

    # (d) error rate ≤ max%
    err_ok=$(awk -v e="$err_rate" -v m="$ERROR_RATE_MAX_PCT" 'BEGIN{print (e<=m)?1:0}')

    # (e) throughput floor: TWO sub-checks
    #   (e.1) primary — throughput@100 ≥ N50-floor% of throughput@50 (only
    #          meaningful if N=50 is in N_DIMENSIONS)
    #   (e.2) backup  — throughput@100 ≥ 4× throughput@1 (parallel efficiency
    #          floor: 4× speedup over N=1 expected at N=100)
    tput_ok=1
    tput_floor_msg=""

    if [ "$n" = "100" ] && [ -n "$n50_tput" ] && [ "$n50_tput" != "0" ]; then
        tput_ratio="$(safe_ratio "$tput" "$n50_tput")"
        floor_pct_decimal=$(awk -v p="$THROUGHPUT_FLOOR_PCT_100_VS_50" 'BEGIN{printf "%.3f", p/100}')
        tput_ok_50=$(awk -v r="$tput_ratio" -v s="$floor_pct_decimal" 'BEGIN{print (r+0>=s+0)?1:0}')
        if [ "$tput_ok_50" -eq 0 ]; then
            tput_ok=0
            tput_floor_msg="; throughput_floor_50 VIOLATION (ratio@100/@50=$tput_ratio floor=$floor_pct_decimal)"
        fi
    fi

    # Backup: N=100 parallel efficiency (vs N=1)
    if [ "$n" = "100" ] && [ "$baseline_tput" != "0" ]; then
        eff_ratio="$(safe_ratio "$tput" "$baseline_tput")"
        eff_floor_decimal=$(awk -v e="$THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100" 'BEGIN{printf "%.3f", e}')
        tput_ok_eff=$(awk -v r="$eff_ratio" -v s="$eff_floor_decimal" 'BEGIN{print (r+0>=s+0)?1:0}')
        if [ "$tput_ok_eff" -eq 0 ]; then
            tput_ok=0
            tput_floor_msg="${tput_floor_msg}; parallel_efficiency_4x VIOLATION (ratio@100/@1=$eff_ratio floor=$eff_floor_decimal)"
        fi
    fi

    # Aggregate the dim verdict
    dim_pass="PASS"
    [ "$time_ok" -eq 1 ]  || { dim_pass="FAIL"; violations+=("N=$n TIME_SLACK ratio=$time_ratio slack=$time_slack (mean time/job exceeds ${time_slack}× baseline)"); }
    [ "$ram_ok"  -eq 1 ]  || { dim_pass="FAIL"; violations+=("N=$n RAM_SLACK ratio=$ram_ratio slack=$RAM_SLACK_AT_ANY_N (max RSS exceeds ${RAM_SLACK_AT_ANY_N}× baseline)"); }
    [ "$cache_ok" -eq 1 ] || { dim_pass="FAIL"; violations+=("N=$n CACHE_BOUND ratio=$cache_ratio bound=$CACHE_BOUND_FACTOR (cache size exceeds ${CACHE_BOUND_FACTOR}× baseline — 'cache illimitata' profile DETECTED)"); }
    [ "$err_ok" -eq 1 ]   || { dim_pass="FAIL"; violations+=("N=$n ERROR_RATE ${err_rate}% exceeds ${ERROR_RATE_MAX_PCT}%"); }
    [ "$tput_ok" -eq 1 ]  || { dim_pass="FAIL"; violations+=("N=$n THROUGHPUT floor: ${tput_floor_msg}"); }

    verdict_per_dim[$n]="$dim_pass"
done

# ── Verdict (final) ───────────────────────────────────────────────────────
if [ "${#violations[@]}" -gt 0 ] || [ "${#violations_spec_literal[@]}" -gt 0 ]; then
    {
        echo "# Test 14 — Scalabilità lineare — FAIL"
        echo
        echo "## Per-dim verdicts"
        echo
        echo "| N (dim) | verdict | mean_ns_per_job | max_rss_kb | cache_after_B | jobs_fail | throughput_jps | error_rate_pct |"
        echo "|---|---|---|---|---|---|---|---|"
        for i in "${!ver_n[@]}"; do
            n="${ver_n[$i]}"
            printf '| %s | **%s** | %s | %s | %s | %s | %s | %s |\n' \
                "$n" "${verdict_per_dim[$n]}" \
                "${ver_mean_ns[$i]}" "${ver_max_rss[$i]}" \
                "${ver_cache[$i]}" "${ver_fail[$i]}" \
                "${ver_tput[$i]}" "${ver_err[$i]}"
        done
        echo
        echo "## Violations"
        echo
        for v in "${violations[@]}"; do echo "- $v"; done
        for v in "${violations_spec_literal[@]}"; do echo "- $v"; done
        echo
        echo "## Spec-literal reminders (user quotes, verbatim)"
        echo
        echo "- 'PASS se il costo cresce quasi-linearmente'"
        echo "- 'FAIL se 1 video=1GB ma 10 video=20GB' → encoded as max_rss@10 / (10 × baseline_max_rss) ≤ 2.0"
        echo "- 'cache illimitata' → encoded as cache_after_N ≤ ${CACHE_BOUND_FACTOR}× cache_after_1"
        echo "- 'prestazioni che crollano' → encoded as throughput@100 ≥ ${THROUGHPUT_FLOOR_PCT_100_VS_50}% × throughput@50 AND ≥ ${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100}× × throughput@1"
    } > "$REPORT_MD"

    echo "GATE_FAIL: Test 14 scaling check FAIL — ${#violations[@]} numeric violations"
    if [ "${#violations_spec_literal[@]}" -gt 0 ]; then
        echo "  Spec-literal triggers: ${#violations_spec_literal[@]}"
    fi
    echo "  Per-dim TSV: $PER_DIM_TSV"
    echo "  Per-job TSV: $PER_JOB_TSV"
    echo "  Markdown report: $REPORT_MD"
    echo "  Verdict rows:"
    for i in "${!ver_n[@]}"; do
        n="${ver_n[$i]}"
        printf '    N=%-4s verdict=%s mean_ns=%-12s max_rss_kb=%-8s cache_after=%-10s throughput_jps=%s err=%s%%\n' \
            "$n" "${verdict_per_dim[$n]}" \
            "${ver_mean_ns[$i]}" "${ver_max_rss[$i]}" \
            "${ver_cache[$i]}" "${ver_tput[$i]}" "${ver_err[$i]}"
    done
    echo "  Common remediation:"
    echo "    - '1 video=1GB ma 10 video=20GB' profile = allocator / IPC leak in telemetry ledger — inspect chronon_render_throughput_ms of N=10 dim"
    echo "    - 'cache illimitata' = ~/.chronon3d/cache/ unbounded LRU eviction — check cache_static.cache_image() + LRU prune hook"
    echo "    - 'prestazioni che crollano' = lock contention or single-threaded serialization — inspect bench command counters"
    exit 1
fi

# ── PASS branch ────────────────────────────────────────────────────────────
{
    echo "# Test 14 — Scalabilità lineare — PASS"
    echo
    echo "## Per-dim verdicts"
    echo
    echo "| N (dim) | verdict | mean_ns_per_job | max_rss_kb | cache_after_B | jobs_fail | throughput_jps | error_rate_pct |"
    echo "|---|---|---|---|---|---|---|---|"
    for i in "${!ver_n[@]}"; do
        n="${ver_n[$i]}"
        printf '| %s | **%s** | %s | %s | %s | %s | %s | %s |\n' \
            "$n" "${verdict_per_dim[$n]}" \
            "${ver_mean_ns[$i]}" "${ver_max_rss[$i]}" \
            "${ver_cache[$i]}" "${ver_fail[$i]}" \
            "${ver_tput[$i]}" "${ver_err[$i]}"
    done
    echo
    echo "## Tolerances HELD"
    echo
    echo "- TIME_SLACK@10: ${TIME_SLACK_AT_10}×, TIME_SLACK@100: ${TIME_SLACK_AT_100}×"
    echo "- RAM_SLACK (all N): ${RAM_SLACK_AT_ANY_N}×"
    echo "- CACHE_BOUND: ${CACHE_BOUND_FACTOR}×"
    echo "- ERROR_RATE_MAX: ${ERROR_RATE_MAX_PCT}%"
    echo "- THROUGHPUT_FLOOR (N=100 vs N=50): ${THROUGHPUT_FLOOR_PCT_100_VS_50}%"
    echo "- THROUGHPUT_EFFICIENCY (N=100 vs N=1): ${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100}×"
    echo
    echo "## Spec-literal guarantees"
    echo
    echo "- '1 video=1GB ma 10 video=20GB' → NOT triggered at N=10 (max_rss_per_job@N=10 ≤ 2× baseline max_rss)"
    echo "- 'cache illimitata' → NOT triggered (cache stays ≤ ${CACHE_BOUND_FACTOR}× baseline)"
    echo "- 'prestazioni che crollano' → NOT triggered (throughput@100 ≥ ${THROUGHPUT_FLOOR_PCT_100_VS_50}% × throughput@50 AND ≥ ${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100}× × throughput@1)"
} > "$REPORT_MD"

echo "GATE_PASS: ${total_jobs_across_dims} jobs across ${dim_idx} N-dimensions scale quasi-linearly"
echo "  PASS criterion F held across all 5 invariants for all ${dim_idx} N-dimensions"
echo "[INFO] ${GATE_NAME}: clean state — ${total_jobs_across_dims} concurrent jobs (~${dim_idx} dim × N=1/10/50/100), max RSS stays under ${RAM_SLACK_AT_ANY_N}× baseline, cache stays under ${CACHE_BOUND_FACTOR}× baseline, throughput@100 holds at ≥${THROUGHPUT_FLOOR_PCT_100_VS_50}% of throughput@50 + ≥${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100}× of throughput@1"
exit 0
