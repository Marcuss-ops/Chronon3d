#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════
# tools/selftest_validate_benchmark_json.sh — F1.2 self-test
# (TICKET-BENCH-SCHEMA-V1)
# ════════════════════════════════════════════════════════════════════════════
#
# Round-trip test for tools/validate_benchmark_json.sh + bench/benchmark_schema.json.
# Synthesizes 4 test fixtures at runtime (no checked-in fixtures; Cat-3
# anti-dup: bench/example_report.json is the only checked-in fixture).
#
# Exit codes (canonical 3-state envelope per AGENTS.md §honesty):
#   0 = SELFTEST_PASS  (all 5 TEST_CASEs clear)
#   1 = SELFTEST_FAIL  (≥1 TEST_CASE failed)
#   2 = SELFTEST_BLOCKED  (validator / schema / python3 unavailable)
# ════════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="selftest_validate_benchmark_json"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Canonical REPO_ROOT pattern (mirrors tools/benchmark_host_info.sh).  See
# the matching comment in tools/validate_benchmark_json.sh for the bash
# `|| cd ... && pwd` pitfall rationale.
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VALIDATOR="${SCRIPT_DIR}/validate_benchmark_json.sh"
SCHEMA="${REPO_ROOT}/bench/benchmark_schema.json"
EXAMPLE="${REPO_ROOT}/bench/example_report.json"

# ── Prereq checks ──────────────────────────────────────────────────────────
if [[ ! -x "$VALIDATOR" ]]; then
    echo "[ERROR] $(basename "$0"): validator not executable: $VALIDATOR" >&2
    exit 2
fi
if [[ ! -r "$SCHEMA" ]]; then
    echo "[ERROR] $(basename "$0"): schema not readable: $SCHEMA" >&2
    exit 2
fi
if [[ ! -r "$EXAMPLE" ]]; then
    echo "[ERROR] $(basename "$0"): example report not readable: $EXAMPLE" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "[ERROR] $(basename "$0"): python3 not in PATH" >&2
    exit 2
fi

# ── helpers ────────────────────────────────────────────────────────────────
_pass=0
_fail=0
_blocked=0
TEST_DIR="$(mktemp -d -t selftest_bench_schema.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT

case_run() {
    local label="$1" expected_rc="$2" actual_rc="$3" detail="${4:-}"
    if [[ "$actual_rc" -eq "$expected_rc" ]]; then
        echo "  [PASS]    $label  (rc=$actual_rc)  $detail"
        _pass=$((_pass + 1))
    else
        echo "  [FAIL]    $label  expected_rc=$expected_rc actual_rc=$actual_rc  $detail"
        _fail=$((_fail + 1))
    fi
}

echo "[INFO] $(basename "$0" .sh): starting 5 TEST_CASEs against $VALIDATOR"
echo "[INFO]   schema: $SCHEMA"
echo "[INFO]   example: $EXAMPLE"
echo "[INFO]   temp test_dir: $TEST_DIR"

# ── TEST CASE 1: schema parses as valid JSON + Draft 2020-12 dialect ──────
if python3 -c "
import json
with open('$SCHEMA', 'r', encoding='utf-8') as fh:
    s = json.load(fh)
assert s.get('\$schema') == 'https://json-schema.org/draft/2020-12/schema', f'unexpected dialect: {s.get(\"\$schema\")}'
assert isinstance(s.get('required'), list) and len(s['required']) == 16, f'expected 16 required fields, got {len(s.get(\"required\", []))}'
assert s.get('additionalProperties') is False, 'expected additionalProperties: false'
" >/dev/null 2>&1; then
    echo "  [PASS]    TEST_CASE 1/5  schema-parses-as-Draft-2020-12 (16 required, additionalProperties:false)"
    _pass=$((_pass + 1))
else
    echo "  [FAIL]    TEST_CASE 1/5  schema-parses-as-Draft-2020-12"
    _fail=$((_fail + 1))
fi

# ── TEST CASE 2: example_report.json validates clean (GATE_PASS exit 0) ──
"$VALIDATOR" --report "$EXAMPLE" >"$TEST_DIR/case2.stdout" 2>"$TEST_DIR/case2.stderr"
rc=$?
case_run "TEST_CASE 2/5  example_report.json validates GATE_PASS" 0 "$rc" "(stdout lines: $(wc -l <"$TEST_DIR/case2.stdout"))"

# ── TEST_CASE 3: missing field → GATE_FAIL exit 1 ────────────────────────
missing_field_json="$TEST_DIR/missing_field.json"
python3 -c "
import json
with open('$EXAMPLE', 'r', encoding='utf-8') as fh:
    d = json.load(fh)
del d['output_hash']
with open('$missing_field_json', 'w', encoding='utf-8') as fh:
    json.dump(d, fh)
"
"$VALIDATOR" --report "$missing_field_json" >"$TEST_DIR/case3.stdout" 2>"$TEST_DIR/case3.stderr"
rc=$?
if grep -q "missing-required" "$TEST_DIR/case3.stdout"; then
    detail="(stdout has 'missing-required' diagnostic ✓)"
else
    detail="(stdout MISSING 'missing-required' diagnostic ✗)"
    rc=99
fi
case_run "TEST_CASE 3/5  missing-field → GATE_FAIL" 1 "$rc" "$detail"

# ── TEST_CASE 4: type mismatch → GATE_FAIL exit 1 ─────────────────────────
wrong_type_json="$TEST_DIR/wrong_type.json"
python3 -c "
import json
with open('$EXAMPLE', 'r', encoding='utf-8') as fh:
    d = json.load(fh)
d['threads'] = 'not-an-integer'
with open('$wrong_type_json', 'w', encoding='utf-8') as fh:
    json.dump(d, fh)
"
"$VALIDATOR" --report "$wrong_type_json" >"$TEST_DIR/case4.stdout" 2>"$TEST_DIR/case4.stderr"
rc=$?
if grep -q "type-mismatch" "$TEST_DIR/case4.stdout"; then
    detail="(stdout has 'type-mismatch' diagnostic ✓)"
else
    detail="(stdout MISSING 'type-mismatch' diagnostic ✗)"
    rc=99
fi
case_run "TEST_CASE 4/5  type-mismatch (threads=string) → GATE_FAIL" 1 "$rc" "$detail"

# ── TEST_CASE 5: unreadable file → GATE_BLOCKED exit 2 ───────────────────
"$VALIDATOR" --report "/nonexistent/path/that/does/not/exist.json" >"$TEST_DIR/case5.stdout" 2>"$TEST_DIR/case5.stderr"
rc=$?
case_run "TEST_CASE 5/5  unreadable-file → GATE_BLOCKED" 2 "$rc" "(stderr lines: $(wc -l <"$TEST_DIR/case5.stderr"))"

# ── summary ────────────────────────────────────────────────────────────────
total=$((_pass + _fail + _blocked))
echo
if [[ "$_fail" -eq 0 ]]; then
    echo "GATE_PASS: $_pass/$total selftest TEST_CASEs cleared"
    echo "[INFO] ${GATE_NAME}: $_pass/$total selftest cases pass (5-case envelope)"
    exit 0
else
    echo "GATE_FAIL: $_fail/$total selftest TEST_CASEs failed (pass=$_pass fail=$_fail blocked=$_blocked)"
    exit 1
fi
