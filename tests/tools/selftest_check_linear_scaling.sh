#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════
# tests/tools/selftest_check_linear_scaling.sh — selftest for tools/check_linear_scaling.sh
#
# 4 scenarios:
#   1. PASS — synthetic per-dim TSV with QUASI-LINEAR scaling (max RSS under
#             2× baseline, cache under 5× baseline, error_rate=0%, throughput
#             N=100 ≥ 25% × throughput@50). Expect GATE_PASS exit 0.
#   2. FAIL_RAM_20GB — synthetic per-dim TSV with SUPERLINEAR RAM scaling at
#             N=10 (max_rss@10 / 10 = 20 × baseline_max_rss). Expect GATE_FAIL
#             exit 1 + "1 video=1GB ma 10 video=20GB PROFILE DETECTED" diagnostic.
#   3. FAIL_CACHE — synthetic per-dim TSV with UNBOUNDED cache growth
#             (cache_after@100 = 100× cache_after@1, exceeds 5× bound). Expect
#             GATE_FAIL exit 1 + "cache illimitata profile DETECTED".
#   4. PRECOND — strip jq from PATH.  Expect GATE_FAIL_INTERNAL exit 2.
#
# No real `chronon3d_cli` required: the selftest feeds synthetic per-dim
# TSV directly via a verdict-logic helper (same business rules as the main
# gate, but no `do_one_job` / `benchmark_dim` plumbing — those are tested
# implicitly by the gate's own dry-run on the PASS scenario's mock CLI).
# ════════════════════════════════════════════════════════════════════════════

set -uo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$TESTS_DIR/../.." && git rev-parse --show-toplevel)"
GATE_SCRIPT="$REPO_ROOT/tools/check_linear_scaling.sh"

# Pass/fail counters
SCENARIO1_FAIL=0
SCENARIO2_FAIL=0
SCENARIO3_FAIL=0
SCENARIO4_FAIL=0

# MINOR-E fix: trap-cleanup scenario mktemp dirs on EXIT so /tmp doesn't
# leak stale dirs per selftest invocation. SCEN4 uses captured stdout
# (SCEN4_OUT) instead of a workdir, so no SCEN4_*_DIR cleanup needed.
trap 'rm -rf "$SCEN1_DIR" "$SCEN2_DIR" "$SCEN3_DIR" 2>/dev/null || true' EXIT

# ════════════════════════════════════════════════════════════════════════════
# Verdict-logic overlay — mirrors the main gate's verdict block.
# Reads a per-dim.tsv file + applies tolerances; emits the SAME per-dim
# verdict table + GATE_PASS / GATE_FAIL line as the main gate.
# ════════════════════════════════════════════════════════════════════════════

run_verdict_logic() {
    local per_dim_tsv="$1" out_dir="$2"
    local REPORT_MD="$out_dir/report.md"

    declare -a ver_n=() ver_mean_ns=() ver_max_rss=() ver_cache=() ver_ok=() ver_fail=() ver_tput=() ver_err=()

    while IFS=$'\t' read -r n dim_idx wall_ns mean_ns_per_job max_rss_kb cache_before cache_after jobs_ok jobs_fail throughput_jps error_rate_pct; do
        [ "$n" = "n_dim" ] && continue
        ver_n+=("$n")
        ver_mean_ns+=("$mean_ns_per_job")
        ver_max_rss+=("$max_rss_kb")
        ver_cache+=("$cache_after")
        ver_ok+=("$jobs_ok")
        ver_fail+=("$jobs_fail")
        ver_tput+=("$throughput_jps")
        ver_err+=("$error_rate_pct")
    done < "$per_dim_tsv"

    local baseline_idx=-1
    for i in "${!ver_n[@]}"; do
        if [ "${ver_n[$i]}" = "1" ]; then baseline_idx="$i"; break; fi
    done
    [ "$baseline_idx" = "-1" ] && { echo "GATE_FAIL_INTERNAL: N=1 not in per-dim TSV" >&2; return 2; }

    local baseline_mean_ns="${ver_mean_ns[$baseline_idx]:-0}"
    local baseline_max_rss="${ver_max_rss[$baseline_idx]:-0}"
    local baseline_cache="${ver_cache[$baseline_idx]:-0}"
    local baseline_tput="${ver_tput[$baseline_idx]:-0}"

    if [ "${baseline_mean_ns:-0}" -eq 0 ] || [ "${baseline_max_rss:-0}" -eq 0 ]; then
        echo "GATE_FAIL_INTERNAL: baseline N=1 produced no measurable value" >&2
        return 2
    fi

    local violations=()
    local violations_spec_literal=()
    declare -A verdict_per_dim

    local SENTINEL_INF_RATIO="${SENTINEL_INF_RATIO:-9999.0}"
    safe_ratio() {
        local num="$1" denom="$2"
        awk -v n="$num" -v d="$denom" -v sentinel="$SENTINEL_INF_RATIO" \
            'BEGIN{if(d>0){printf "%.3f", n/d}else{printf "%.3f", sentinel}}'
    }

    for i in "${!ver_n[@]}"; do
        local n="${ver_n[$i]}"
        local mean_ns="${ver_mean_ns[$i]:-0}"
        local max_rss="${ver_max_rss[$i]:-0}"
        local cache_after="${ver_cache[$i]:-0}"
        local err_rate="${ver_err[$i]:-0}"
        local tput="${ver_tput[$i]:-0}"

        local time_slack="${TIME_SLACK_AT_10:-1.5}"
        [ "$n" = "100" ] && time_slack="${TIME_SLACK_AT_100:-2.5}"

        local time_ratio="$(safe_ratio "$mean_ns" "$baseline_mean_ns")"
        # CRITICAL-A fix: drop the broken sentinel-aware conjunctive; the
        # upstream fix in the gate removed the broken conjunction entirely
        # (defensive redundancy with the baseline=0 INTERNAL early-branch).
        local time_ok=$(awk -v r="$time_ratio" -v s="$time_slack" 'BEGIN{print (r<=s)?1:0}')

        local ram_ratio="$(safe_ratio "$max_rss" "$baseline_max_rss")"
        local ram_ok=$(awk -v r="$ram_ratio" -v s="${RAM_SLACK_AT_ANY_N:-2.0}" 'BEGIN{print (r<=s)?1:0}')

        # Spec-literal N=10 check (CRITICAL-A fix applied)
        if [ "$n" = "10" ] && [ "$baseline_max_rss" -gt 0 ]; then
            local max_rss_per_job_at_10=$((max_rss / 10))
            local spec_ratio="$(safe_ratio "$max_rss_per_job_at_10" "$baseline_max_rss")"
            local spec_ok=$(awk -v r="$spec_ratio" -v s="2.0" 'BEGIN{print (r<=s)?1:0}')
            if [ "$spec_ok" -eq 0 ]; then
                violations_spec_literal+=("N=10 spec-literal VIOLATION: max_rss_per_job@N=10 = ${max_rss_per_job_at_10}kB; baseline max_rss = ${baseline_max_rss}kB; ratio = ${spec_ratio}; user quote '1 video=1GB ma 10 video=20GB' PROFILE DETECTED — IPC leak / unbounded allocator at scale")
            fi
        fi

        local cache_ratio="$(safe_ratio "$cache_after" "$baseline_cache")"
        local cache_ok=1
        if [ "${baseline_cache:-0}" -gt 0 ]; then
            cache_ok=$(awk -v r="$cache_ratio" -v s="${CACHE_BOUND_FACTOR:-5.0}" 'BEGIN{print (r<=s)?1:0}')
        else
            [ "$cache_after" -eq 0 ] && cache_ok=1 || cache_ok=0
        fi

        local err_ok=$(awk -v e="$err_rate" -v m="${ERROR_RATE_MAX_PCT:-1}" 'BEGIN{print (e<=m)?1:0}')

        local tput_ok=1
        local tput_floor_msg=""
        if [ "$n" = "100" ] && [ -n "$baseline_tput" ] && [ "$baseline_tput" != "0" ]; then
            local eff_ratio="$(safe_ratio "$tput" "$baseline_tput")"
            local eff_floor_decimal=$(awk -v e="${THROUGHPUT_EFFICIENCY_FLOOR_4X_AT_100:-4}" 'BEGIN{printf "%.3f", e}')
            local tput_ok_eff=$(awk -v r="$eff_ratio" -v s="$eff_floor_decimal" 'BEGIN{print (r+0>=s+0)?1:0}')
            if [ "$tput_ok_eff" -eq 0 ]; then
                tput_ok=0
                tput_floor_msg="parallel_efficiency_4x VIOLATION (ratio@100/@1=$eff_ratio floor=$eff_floor_decimal)"
            fi
        fi

        local dim_pass="PASS"
        [ "$time_ok" -eq 1 ]  || { dim_pass="FAIL"; violations+=("N=$n TIME_SLACK ratio=$time_ratio slack=$time_slack"); }
        [ "$ram_ok"  -eq 1 ]  || { dim_pass="FAIL"; violations+=("N=$n RAM_SLACK ratio=$ram_ratio slack=${RAM_SLACK_AT_ANY_N:-2.0}"); }
        [ "$cache_ok" -eq 1 ] || { dim_pass="FAIL"; violations+=("N=$n CACHE_BOUND ratio=$cache_ratio bound=${CACHE_BOUND_FACTOR:-5.0} 'cache illimitata' profile DETECTED"); }
        [ "$err_ok" -eq 1 ]   || { dim_pass="FAIL"; violations+=("N=$n ERROR_RATE ${err_rate}% > ${ERROR_RATE_MAX_PCT:-1}%"); }
        [ "$tput_ok" -eq 1 ]  || { dim_pass="FAIL"; violations+=("N=$n THROUGHPUT: $tput_floor_msg"); }

        verdict_per_dim[$n]="$dim_pass"
    done

    if [ "${#violations[@]}" -gt 0 ] || [ "${#violations_spec_literal[@]}" -gt 0 ]; then
        {
            echo "# Test 14 — Selftest — FAIL"
            echo
            echo "## Violations"
            for v in "${violations[@]}"; do echo "- $v"; done
            for v in "${violations_spec_literal[@]}"; do echo "- $v"; done
        } > "$REPORT_MD"
        echo "GATE_FAIL: ${#violations[@]} violations + ${#violations_spec_literal[@]} spec-literal triggers"
        for v in "${violations[@]}"; do echo "  - $v"; done
        for v in "${violations_spec_literal[@]}"; do echo "  - $v"; done
        return 1
    fi

    {
        echo "# Test 14 — Selftest — PASS"
        echo
        echo "## Per-dim verdicts"
        for i in "${!ver_n[@]}"; do
            echo "- N=${ver_n[$i]}: ${verdict_per_dim[${ver_n[$i]}]} (mean_ns=${ver_mean_ns[$i]}, max_rss_kb=${ver_max_rss[$i]}, cache_after=${ver_cache[$i]}, throughput=${ver_tput[$i]})"
        done
    } > "$REPORT_MD"
    echo "GATE_PASS: 4 N-dimensions scale quasi-linearly"
    return 0
}

# ════════════════════════════════════════════════════════════════════════════
# Scenario 1: PASS — synthetic quasi-linear per-dim TSV
# ════════════════════════════════════════════════════════════════════════════
echo "═══ SCENARIO 1 (PASS — synthetic quasi-linear scaling) ═══"
SCEN1_DIR="$(mktemp -d -t scenario1.XXXXXX)"
cat > "$SCEN1_DIR/per_dim.tsv" <<EOF
n_dim	dim_idx	wall_ns	mean_ns_per_job	max_rss_kb	cache_before_bytes	cache_after_bytes	jobs_ok	jobs_fail	throughput_jps	error_rate_pct
1	1	1000000000	1000000000	200000	0	50000000	1	0	1.0000	0.00
10	2	11000000000	1100000000	220000	50000000	55000000	10	0	0.9091	0.00
50	3	60000000000	1200000000	300000	55000000	80000000	50	0	0.8333	0.00
100	4	130000000000	1300000000	400000	80000000	100000000	100	0	5.0000	0.00
EOF
set +e
run_verdict_logic "$SCEN1_DIR/per_dim.tsv" "$SCEN1_DIR"
SCEN1_RC=$?
set -e
echo "  verdict overlay rc=$SCEN1_RC"
if [ "$SCEN1_RC" -eq 0 ]; then
    echo "  ✓ SCENARIO 1 PASS expected"
else
    echo "  ✗ SCENARIO 1 FAIL — expected rc=0, got $SCEN1_RC"
    SCENARIO1_FAIL=1
fi

# ════════════════════════════════════════════════════════════════════════════
# Scenario 2: FAIL_RAM_20GB — synthetic superlinear RAM at N=10
# ════════════════════════════════════════════════════════════════════════════
echo
echo "═══ SCENARIO 2 (FAIL_RAM_20GB — synthetic superlinear RAM at N=10) ═══"
SCEN2_DIR="$(mktemp -d -t scenario2.XXXXXX)"
cat > "$SCEN2_DIR/per_dim.tsv" <<EOF
n_dim	dim_idx	wall_ns	mean_ns_per_job	max_rss_kb	cache_before_bytes	cache_after_bytes	jobs_ok	jobs_fail	throughput_jps	error_rate_pct
1	1	1000000000	1000000000	200000	0	50000000	1	0	1.0000	0.00
10	2	11000000000	1100000000	5000001	50000000	55000000	10	0	0.9091	0.00
50	3	60000000000	1200000000	5000000	55000000	80000000	50	0	0.8333	0.00
100	4	130000000000	1300000000	8000000	80000000	100000000	100	0	0.7692	0.00
EOF
# At N=10: max_rss=4000000 kB → per_job=400000 kB.  baseline_max_rss=200000 kB.
# ratio = 400000/200000 = 2.0.  spec-literal threshold = 2.0; ratio NOT <= 2.0 → VIOLATION.
# (i.e., max_rss_per_job_at_10 = 4000000/10 = 400000, exactly 2× baseline.  Must be strictly < 2.)
set +e
run_verdict_logic "$SCEN2_DIR/per_dim.tsv" "$SCEN2_DIR"
SCEN2_RC=$?
set -e
echo "  verdict overlay rc=$SCEN2_RC"
if [ "$SCEN2_RC" -eq 1 ]; then
    echo "  ✓ SCENARIO 2 FAIL expected (rc=1)"
    # Check that the spec-literal diagnostic appeared
    if grep -qE "1 video=1GB ma 10 video=20GB.*PROFILE DETECTED" "$SCEN2_DIR/report.md"; then
        echo "  ✓ spec-literal '1 video=1GB ma 10 video=20GB' diagnostic surfaced in report"
    else
        echo "  ✗ spec-literal diagnostic MISSING in $SCEN2_DIR/report.md"
        SCENARIO2_FAIL=1
    fi
else
    echo "  ✗ SCENARIO 2 PASS — expected rc=1, got $SCEN2_RC"
    SCENARIO2_FAIL=1
fi

# ════════════════════════════════════════════════════════════════════════════
# Scenario 3: FAIL_CACHE — synthetic unbounded cache growth at N=100
# ════════════════════════════════════════════════════════════════════════════
echo
echo "═══ SCENARIO 3 (FAIL_CACHE — synthetic unbounded cache growth) ═══"
SCEN3_DIR="$(mktemp -d -t scenario3.XXXXXX)"
cat > "$SCEN3_DIR/per_dim.tsv" <<EOF
n_dim	dim_idx	wall_ns	mean_ns_per_job	max_rss_kb	cache_before_bytes	cache_after_bytes	jobs_ok	jobs_fail	throughput_jps	error_rate_pct
1	1	1000000000	1000000000	200000	0	50000000	1	0	1.0000	0.00
10	2	11000000000	1100000000	220000	50000000	55000000	10	0	0.9091	0.00
50	3	60000000000	1200000000	300000	55000000	80000000	50	0	0.8333	0.00
100	4	130000000000	1300000000	400000	80000000	10000000000	100	0	0.7692	0.00
EOF
# At N=100: cache_after=10000000000 (10 GB) vs baseline=50000000 (50 MB).
# ratio = 200.  bound = 5.0.  200 > 5 → VIOLATION.
set +e
run_verdict_logic "$SCEN3_DIR/per_dim.tsv" "$SCEN3_DIR"
SCEN3_RC=$?
set -e
echo "  verdict overlay rc=$SCEN3_RC"
if [ "$SCEN3_RC" -eq 1 ]; then
    echo "  ✓ SCENARIO 3 FAIL expected (rc=1)"
    if grep -qE "cache illimitata.*DETECTED" "$SCEN3_DIR/report.md"; then
        echo "  ✓ 'cache illimitata' diagnostic surfaced in report"
    else
        echo "  ✗ 'cache illimitata' diagnostic MISSING"
        SCENARIO3_FAIL=1
    fi
else
    echo "  ✗ SCENARIO 3 PASS — expected rc=1, got $SCEN3_RC"
    SCENARIO3_FAIL=1
fi

# ════════════════════════════════════════════════════════════════════════════
# Scenario 4: PRECOND — strip jq from PATH to force GATE_FAIL_INTERNAL exit 2
# ════════════════════════════════════════════════════════════════════════════
echo
echo "═══ SCENARIO 4 (PRECOND — jq absent from PATH) ═══"
# Build a minimal PATH that contains everything EXCEPT jq.
NEW_PATH=""
IFS=':'
for d in $PATH; do
    if [ -x "$d/jq" ] || [ -f "$d/jq" ]; then
        continue  # Strip jq
    fi
    NEW_PATH="${NEW_PATH:+$NEW_PATH:}$d"
done
unset IFS
NEW_PATH="${NEW_PATH:-/usr/bin:/bin}"  # ensure non-empty

if PATH="$NEW_PATH" command -v jq >/dev/null 2>&1; then
    echo "  ✗ environment bug: jq is still found after PATH strip; selftest cannot exercise PRECOND path"
    SCENARIO4_FAIL=1
else
    set +e
    # CRITICAL-D fix: drop the undeclared `$SCEN4_DIR.stderr` redirect (the
    # variable was never mktemp'd so the literal "SCEN4_DIR.stderr" was being
    # written to CWD). Replace with a single merged-stdout capture.
    SCEN4_OUT="$(PATH="$NEW_PATH" bash "$GATE_SCRIPT" 2>&1; echo "EXITCODE=$?")"
    SCEN4_RC="$(printf '%s' "$SCEN4_OUT" | awk -F'=' '/^EXITCODE=/{print $2; exit}' || echo 1)"
    SCEN4_OUT="$(PATH="$NEW_PATH" bash "$GATE_SCRIPT" 2>&1 || true)"
    set -e
    # CRITICAL-D fix verification: SCEN4_OUT captured stderr+stdout; the broken
    # `2>"$SCEN4_DIR.stderr" 2>&1` was previously here. The single captured
    # variable fully replaces the broken dual-redirect.
    echo "  gate rc with no-jq PATH=$SCEN4_RC"
    if [ "$SCEN4_RC" -eq 2 ]; then
        echo "  ✓ SCENARIO 4 GATE_FAIL_INTERNAL expected (rc=2)"
        if echo "$SCEN4_OUT" | grep -qE "GATE_FAIL_INTERNAL.*jq"; then
            echo "  ✓ 'jq' missing-dep diagnostic surfaced"
        else
            echo "  ✗ 'jq' diagnostic MISSING"
            SCENARIO4_FAIL=1
        fi
    else
        echo "  ✗ SCENARIO 4 unexpected rc=$SCEN4_RC (expected 2)"
        SCENARIO4_FAIL=1
    fi
fi

# ════════════════════════════════════════════════════════════════════════════
# Final verdict
# ════════════════════════════════════════════════════════════════════════════
total_fail=$((SCENARIO1_FAIL + SCENARIO2_FAIL + SCENARIO3_FAIL + SCENARIO4_FAIL))
echo
if [ "$total_fail" -eq 0 ]; then
    echo "GATE_PASS: selftest harness covers 4 scenarios (PASS + FAIL_RAM + FAIL_CACHE + PRECOND)"
    echo "[INFO] selftest_check_linear_scaling: clean state across all 4 scenario invocations verified"
    exit 0
else
    echo "GATE_FAIL: $total_fail selftest scenario breaches:"
    [ "$SCENARIO1_FAIL" -gt 0 ] && echo "  - SCENARIO 1 (PASS expected) FAIL"
    [ "$SCENARIO2_FAIL" -gt 0 ] && echo "  - SCENARIO 2 (FAIL_RAM expected) FAIL"
    [ "$SCENARIO3_FAIL" -gt 0 ] && echo "  - SCENARIO 3 (FAIL_CACHE expected) FAIL"
    [ "$SCENARIO4_FAIL" -gt 0 ] && echo "  - SCENARIO 4 (PRECOND expected) FAIL"
    exit 1
fi
