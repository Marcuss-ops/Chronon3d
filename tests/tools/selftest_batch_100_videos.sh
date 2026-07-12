#!/usr/bin/env bash
# ============================================================================
# selftest_batch_100_videos.sh — 4-scenario verdict-logic test for Test #20
# ============================================================================
# Tests tools/check_batch_100_videos.sh against 4 PASS/FAIL scenarios:
#   1. PASS happy path (all-green emission — 100 completed / 0 fail)
#   2. FAIL on 1 crash             (zero_crashes breach)
#   3. FAIL on 1 corrupted         (zero_corrupted breach — waives output_count by emitting completed=0 at the corrupted job)
#   4. FAIL on 3 manual_touches   (at_least_98_pct_no_manual breach)
#
# Per AGENTS.md `## Regole di lint documentale` ### INFO-level diagnostic style:
# emits `[INFO] selftest_batch_100_videos: ...` line on PASS addizionale al
# canonico `GATE_PASS`, NEVER on FAIL (the FAIL path stays unchanged).
#
# Per AGENTS.md `### Test binary staleness check (honesty, pre-ctest invariant)`:
# the harness generates deterministic JSONL files in a tmpdir (NO real
# chronon3d_cli invocation, NO real renders) and feeds them via
# BATCH_100_VIDEOS_LOG env override — the gate's verdict logic is exercised
# without depending on build host state.
# ============================================================================

set -uo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GATE_SCRIPT="$REPO_ROOT/tools/check_batch_100_videos.sh"
GATE_NAME=selftest_batch_100_videos

if [ ! -x "$GATE_SCRIPT" ]; then
    echo "SELFTEST GATE_FAIL: gate script missing or not executable: $GATE_SCRIPT" >&2
    exit 2
fi

# ── Mock JSONL generator (mirrors tests/acceptance/test_batch_100_videos.cpp's ──
# run_stub_batch() deterministic metric emission):
#   fail_mode = 0 → all green                 (100 completed, 0 failures)
#   fail_mode = 1 → crash at job 50           (completed=99, failed=1, crashes=1)
#   fail_mode = 2 → corrupt at job 33         (completed=99, failed=1, corrupted=1)
#   fail_mode = 4 → first 3 jobs get manual   (manual_touches=3, completed=100)
generate_jsonl() {
    local fail_mode="$1"
    local out_path="$2"
    local langs=(en it es fr de pt ja zh ru ar)
    local topics=(tech_review tutorial behind_the_scenes news_brief motivation \
                  countdown recipe travel tutorial_code promo)
    : > "$out_path"
    local counter=0
    for lang in "${langs[@]}"; do
        for topic in "${topics[@]}"; do
            counter=$((counter + 1))
            local comp=1 fail=0 man=0 thr=100 ram=64 cr=0 co=0 inv=0
            if [ "$fail_mode" = "1" ] && [ "$counter" -eq 50 ]; then comp=0; fail=1; cr=1; fi
            if [ "$fail_mode" = "2" ] && [ "$counter" -eq 33 ]; then comp=0; fail=1; co=1; fi
            if [ "$fail_mode" = "4" ] && [ "$counter" -le 3 ]; then man=1; fi
            printf '{"job_index":%d,"lang":"%s","topic":"%s","format":"vertical_9_16","completed":%d,"failed":%d,"manual_touches":%d,"total_time_ms":%d,"peak_RAM_MB":%d,"crashes":%d,"corrupted":%d,"invisible_text":%d}\n' \
                "$counter" "$lang" "$topic" "$comp" "$fail" "$man" "$thr" "$ram" "$cr" "$co" "$inv" \
                >> "$out_path"
        done
    done
}

run_scenario() {
    local name="$1" fail_mode="$2" expect_exit="$3"
    local tmp_log; tmp_log="$(mktemp -t batch_100_selftest_jsonl.XXXXXX)"
    local tmp_yaml; tmp_yaml="$(mktemp -t batch_100_selftest_yaml.XXXXXX)"
    cat > "$tmp_yaml" <<'EOF'
batch_corpus_schema_version: "batch_100_videos_corpus_v1_selftest"
pass_criteria:
  - id: output_count; field: completed; operator: "=="; expectation: 100
  - id: zero_crashes; field: crashes; operator: "=="; expectation: 0
  - id: zero_corrupted; field: corrupted; operator: "=="; expectation: 0
  - id: at_least_98_pct_no_manual; field: manual_touches; operator: "<="; expectation: 2
EOF
    generate_jsonl "$fail_mode" "$tmp_log"
    local rc=0
    BATCH_100_VIDEOS_LOG="$tmp_log" BATCH_100_VIDEOS_CONFIG="$tmp_yaml" \
        bash "$GATE_SCRIPT" > /dev/null 2>&1
    rc=$?
    rm -f "$tmp_log" "$tmp_yaml"
    if [ "$rc" = "$expect_exit" ]; then
        printf 'OK: scenario %s (fail_mode=%s) exit=%d as expected\n' "$name" "$fail_mode" "$rc"
        return 0
    else
        printf 'SELFTEST FAIL: scenario %s (fail_mode=%s) exit=%d (expected %d)\n' \
            "$name" "$fail_mode" "$rc" "$expect_exit" >&2
        return 1
    fi
}

fails=0
# Scenario 1: PASS happy (all-green -> 100 completed)
run_scenario "PASS_happy"    0 0 || fails=$((fails + 1))
# Scenario 2: FAIL_crash (1 crash at job 50 -> completed=99, crashes=1)
run_scenario "FAIL_crash"    1 1 || fails=$((fails + 1))
# Scenario 3: FAIL_corrupt (1 corrupted at job 33 -> completed=99, corrupted=1)
run_scenario "FAIL_corrupt"  2 1 || fails=$((fails + 1))
# Scenario 4: FAIL_manual_3 (3 manual_touches on first 3 -> ≤3 > ≤2)
run_scenario "FAIL_manual_3" 4 1 || fails=$((fails + 1))

if [ "$fails" -eq 0 ]; then
    echo "GATE_PASS: 4/4 scenarios PASS (PASS_happy green path; 3x FAIL_* envelopes correctly detected)"
    echo "[INFO] ${GATE_NAME}: 4/4 scenarios verified — PASS happy detects 4 envelopes emit PASS; FAIL_crash / FAIL_corrupt / FAIL_manual_3 each breach exactly one envelope and emit non-zero exit"
    exit 0
else
    echo "GATE_FAIL: $fails/4 scenarios failed" >&2
    exit 1
fi
