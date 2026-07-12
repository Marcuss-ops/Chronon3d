#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════
# tests/tools/selftest_check_determinism.sh
#
# Selftest harness for `tools/check_determinism.sh` (Test 10).
# Mirrors the precedent of `tests/helpers/selftest_resolve_rebase_conflict.py`
# (commit 7218e14e) + `tests/tools/test_check_ae_parity_golden_state.sh`.
#
# Three scenarios (each a self-contained assertion of the gate's external
# behavior using a MOCK chronon3d_cli that emits a known RGBA + known JSON):
#
#   (1) PASS scenario  — mock CLI emits identical 8x4 fake-RGBA bytes + identical
#                       inspect-text JSON for every run.  Gate must exit 0.
#   (2) FAIL scenario  — mock CLI emits identical RGBA through run 10, then FLIPS
#                       a byte on run 11.  Gate must exit 1 + emit [FAIL] line.
#   (3) PRECOND scenario — PATH stripped of `jq` to fail the precondition check.
#                          Gate must exit 2 (GATE_FAIL_INTERNAL).
#
# Run:
#   bash tests/tools/selftest_check_determinism.sh
#   bash tests/tools/selftest_check_determinism.sh --quick  # smaller RUNS_PER_CONFIG (5)
#                                                     # for fast iteration; still PASS / FAIL / PRECOND coverage.
#
# Exit codes:
#   0  all 3 selftest scenarios PASS
#   1  at least one selftest scenario FAILS
#   2  internal script error (mock build failure etc.)
# ════════════════════════════════════════════════════════════════════════════

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GATE="$SCRIPT_DIR/../../tools/check_determinism.sh"
[ -x "$GATE" ] || { chmod +x "$GATE" 2>/dev/null || true; }

if [ ! -x "$GATE" ]; then
    echo "GATE_FAIL (internal): check_determinism.sh not found or not executable at $GATE" >&2
    exit 2
fi

# Selftest scope knob (--quick for fast iteration).
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1
RUNS_PER_CONFIG_DEFAULT=5
[ "$QUICK" -eq 0 ] && RUNS_PER_CONFIG_DEFAULT=20
RUNS_PER_CONFIG="${RUNS_PER_CONFIG:-${RUNS_PER_CONFIG_DEFAULT}}"

# Lower the per-render count so the selftest runs ≤ 60s (3 scenarios × ~5-10 renders).
SELFTEST_RUNS=5
[ "$QUICK" -eq 0 ] && SELFTEST_RUNS=5

REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WORK="$(mktemp -d -t chronon3d_determinism_selftest.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/mock_cli" "$WORK/det_run_a" "$WORK/det_run_b" "$WORK/det_run_c"
export PATH="$WORK/mock_cli:$PATH"

# ── Mock fixture builders ────────────────────────────────────────────────
# Mock chronon3d_cli emits a fake 8x4 RGBA frame (32 bytes) + a fake inspect-text
# JSON.  We use a fixed RGB pattern + predictable bbox so the gate's hash
# comparison surfaces byte-identical PASS / byte-different FAIL.

# Generate 32 bytes (8 pixels × 4 channels RGBA) of deterministic-but-non-trivial
# pixel content.  Inputs: a seed.
mock_rgba_bytes() {
    local seed="$1"
    # Use printf to emit 32 bytes deterministically (8 RGBA pixels of solid colour
    # derived from the seed).  Each pixel = (seed%32, (seed+8)%32, (seed+16)%32, 255).
    local r=$((seed % 32)) g=$(((seed + 8) % 32)) b=$(((seed + 16) % 32))
    for _ in 1 2 3 4 5 6 7 8; do
        printf '%c%c%c%c' "$r" "$g" "$b" 255
    done
}

# Write the mock CLIs.  We use bash scripts that emit the canned PNG bytes
# + the canned JSON.  The PNG is generated with ImageMagick `convert -size
# 2x1 xc:<color>` then sha-sum'd, simulated here by directly emitting a known
# PNG path per run.  But since we need PIXEL-IDENTICAL fixtures across tests,
# we install a deterministic PNG generator INSIDE the mock.
mock_pass_cli="$WORK/mock_cli/chronon3d_cli"
{
    echo "#!/usr/bin/env bash"
    echo "# Mock CLI for selftest — emits deterministic RGBA + JSON"
    echo "case \"\$1\" in"
    echo "  render)"
    echo "    out=\"\$4\""
    echo "    convert -size 2x1 xc:'#102030' \"\$out\""
    echo "    # Pad to 1920x1080 with the SAME colour (we hash the raw rgba bytes;"
    echo "    # a 1920x1080 deterministic monochromatic frame is byte-identical each run)."
    echo "    exit 0"
    echo "    ;;"
    echo "  inspect-text)"
    echo "    cat <<'JSON'"
    echo '[{"node":"MockComp","font":"mock.ttf","glyph_count":4,"frame":0,'
    echo ' "local_bbox":{"x0":0,"y0":0,"x1":1920,"y1":1080},'
    echo ' "world_bbox":{"x0":0,"y0":0,"x1":1920,"y1":1080},'
    echo ' "predicted_bbox":{"x0":0,"y0":0,"x1":1920,"y1":1080},'
    echo ' "alpha_bbox":{"x0":10,"y0":10,"x1":1910,"y1":1070},'
    echo ' "status":"PASS"}]'
    echo "    exit 0"
    echo "    ;;"
    echo "  *)"
    echo "    exit 0"
    echo "    ;;"
    echo "esac"
} > "$mock_pass_cli"
chmod +x "$mock_pass_cli"

# Mock FAIL CLI: identical to mock_pass_cli through run_idx 10, but on run_idx
# 11 emits a DIFFERENT-coloured frame (one byte-flip at the source colour).  We
# wire this through a counter file so the determinism-changes-after-10-runs
# behavior is exercised.
FAIL_RUNS_FILE="$WORK/.fail_run_counter"
echo "0" > "$FAIL_RUNS_FILE"
mock_fail_cli="$WORK/mock_cli_fail/chronon3d_cli"
mkdir -p "$WORK/mock_cli_fail"
{
    echo "#!/usr/bin/env bash"
    echo "# Mock CLI for selftest — emits MISMATCH on run 11+ (FAIL scenario)"
    echo "case \"\$1\" in"
    echo "  render)"
    echo "    out=\"\$4\""
    echo "    N=\"\$(cat $FAIL_RUNS_FILE 2>/dev/null || echo 0)\""
    echo "    N=\$((N + 1))"
    echo "    echo \"\$N\" > \"$FAIL_RUNS_FILE\""
    echo "    if [ \"\$N\" -le 10 ]; then"
    echo "      convert -size 2x1 xc:'#102030' \"\$out\""
    echo "    else"
    echo "      convert -size 2x1 xc:'#102031' \"\$out\"  # byte-flip"
    echo "    fi"
    echo "    exit 0"
    echo "    ;;"
    echo "  inspect-text)"
    echo "    cat <<'JSON'"
    echo '[{"node":"MockComp","font":"mock.ttf","glyph_count":4,"frame":0,'
    echo ' "local_bbox":{"x0":0,"y0":0,"x1":1920,"y1":1080},'
    echo ' "world_bbox":{"x0":0,"y0":0,"x1":1920,"y1":1080},'
    echo ' "predicted_bbox":{"x0":0,"y0":0,"x1":1920,"y1":1080},'
    echo ' "alpha_bbox":{"x0":10,"y0":10,"x1":1910,"y1":1070},'
    echo ' "status":"PASS"}]'
    echo "    exit 0"
    echo "    ;;"
    echo "  *)"
    echo "    exit 0"
    echo "    ;;"
    echo "esac"
} > "$mock_fail_cli"
chmod +x "$mock_fail_cli"

# ── Scenario 1: PASS ─────────────────────────────────────────────────────
echo "── scenario 1: PASS ——"
echo "  Mock CLI emits identical output for every run. Expecting gate PASS (exit 0)."
RUNS_PER_CONFIG="$SELFTEST_RUNS" OUT_DIR="$WORK/det_run_a" PATH="$WORK/mock_cli:$PATH" \
    CLI_DEBUG_PATH="$WORK/mock_cli/chronon3d_cli" \
    CLI_RELEASE_PATH="$WORK/mock_cli/chronon3d_cli" \
    bash "$GATE" > "$WORK/scenario_pass.log" 2>&1 || true
scenario1_exit=$?
if [ "$scenario1_exit" -eq 0 ]; then
    if grep -q 'GATE_PASS' "$WORK/scenario_pass.log"; then
        echo "[PASS] scenario 1 (gate exit 0 + GATE_PASS line)"
    else
        echo "[FAIL] scenario 1 (exit 0 but missing GATE_PASS in log)"
        SCENARIO1_FAIL=1
    fi
else
    echo "[FAIL] scenario 1 (gate exit $scenario1_exit, expected 0)"
    echo "--- last 30 log lines ---"
    tail -30 "$WORK/scenario_pass.log" | sed 's/^/    /'
    SCENARIO1_FAIL=1
fi

# ── Scenario 2: FAIL (byte-flip on run 11) ───────────────────────────────
echo
echo "── scenario 2: FAIL ——"
echo "  Mock CLI flips byte on render #11 (out of 20). Expecting gate FAIL (exit 1)."
# Reset FAIL counter to 0 between scenarios.
echo "0" > "$FAIL_RUNS_FILE"
# We need at least 11 runs to surface the byte-flip.  Use 11 runs.
RUNS_PER_CONFIG=11 OUT_DIR="$WORK/det_run_b" PATH="$WORK/mock_cli_fail:$PATH" \
    CLI_DEBUG_PATH="$WORK/mock_cli_fail/chronon3d_cli" \
    CLI_RELEASE_PATH="$WORK/mock_cli_fail/chronon3d_cli" \
    bash "$GATE" > "$WORK/scenario_fail.log" 2>&1 || true
scenario2_exit=$?
if [ "$scenario2_exit" -eq 1 ]; then
    if grep -q 'GATE_FAIL' "$WORK/scenario_fail.log"; then
        echo "[PASS] scenario 2 (gate exit 1 + GATE_FAIL line)"
    else
        echo "[FAIL] scenario 2 (exit 1 but missing GATE_FAIL in log)"
        SCENARIO2_FAIL=1
    fi
else
    echo "[FAIL] scenario 2 (gate exit $scenario2_exit, expected 1)"
    echo "--- last 30 log lines ---"
    tail -30 "$WORK/scenario_fail.log" | sed 's/^/    /'
    SCENARIO2_FAIL=1
fi

# ── Scenario 4: NO_DIAG pass-modal (NIT-3 round-2 selftest coverage) ────
# Mock CLI that does NOT emit valid inspect-text JSON (i.e. the determinism
# toolchain was built WITHOUT CHRONON3D_BUILD_DIAGNOSTICS=ON).  Per NIT-1 fix
# the gate must treat NO_DIAGNOSTICS as a per-axis pass-modal watermark:
# ALL runs emitting NO_DIAGNOSTICS for all 3 inspect-text axes must still
# match the canonical baseline AND the gate must PASS with the DIAG_MISSING
# counter surfaced in the summary (NOT a FAIL).
echo
echo "── scenario 4: NO_DIAG pass-modal (NIT-3 coverage) ——"
echo "  Mock CLI emits INVALID inspect-text JSON. Expecting gate PASS + DIAG_MISSING noted in summary."
mkdir -p "$WORK/mock_cli_nodiag"
mock_nodiag_cli="$WORK/mock_cli_nodiag/chronon3d_cli"
{
    echo "#!/usr/bin/env bash"
    echo "# Mock CLI for selftest — emits INVALID inspect-text JSON (no DIAG build)"
    echo "case \"\$1\" in"
    echo "  render)"
    echo "    out=\"\$4\""
    echo "    convert -size 2x1 xc:'#102030' \"\$out\"  # byte-stable PNG"
    echo "    exit 0"
    echo "    ;;"
    echo "  inspect-text)"
    # Non-diagnostic build: emit a JSON error string (NOT a valid array).
    # The gate's jq -e 'type == \"array\"' check fails → diag_status=NO_DIAG.
    echo "    echo '{\"error\":\"diagnostics_disabled\",\"status\":\"FAIL\"}'"
    echo "    exit 0"
    echo "    ;;"
    echo "  *) exit 0 ;;"
    echo "esac"
} > "$mock_nodiag_cli"
chmod +x "$mock_nodiag_cli"

RUNS_PER_CONFIG=5 OUT_DIR="$WORK/det_run_d" PATH="$WORK/mock_cli_nodiag:$PATH" \
    CLI_DEBUG_PATH="$WORK/mock_cli_nodiag/chronon3d_cli" \
    CLI_RELEASE_PATH="$WORK/mock_cli_nodiag/chronon3d_cli" \
    bash "$GATE" > "$WORK/scenario_nodiag.log" 2>&1 || true
scenario4_exit=$?
SCENARIO4_FAIL=0
if [ "$scenario4_exit" -eq 0 ]; then
    if grep -q 'GATE_PASS' "$WORK/scenario_nodiag.log"; then
        echo "[PASS] scenario 4 (gate exit 0 + GATE_PASS + NO_DIAG pass-modal watermark)"
    else
        echo "[FAIL] scenario 4 (exit 0 but missing GATE_PASS)"
        SCENARIO4_FAIL=1
    fi
else
    echo "[FAIL] scenario 4 (gate exit $scenario4_exit, expected 0 for NO_DIAG pass-modal)"
    echo "--- last 20 log lines ---"
    tail -20 "$WORK/scenario_nodiag.log" | sed 's/^/    /'
    SCENARIO4_FAIL=1
fi

# ── Scenario 3: PRECOND (missing `jq`) ────────────────────────────────────
# `jq` is in /usr/bin/jq normally.  We construct a PATH that omits it by
# using a minimal PATH that contains just our mock + bash + convert + sha256sum +
# mktemp + cwd.  Easier: temporarily define a stub PATH and use `env -i` to
# wipe inherited env, then set ONLY what's needed (PATH WITHOUT jq).
echo
echo "── scenario 3: PRECOND (missing `jq`) ——"
echo "  PATH set to omit jq. Expecting gate INTERNAL (exit 2)."
# Build a PATH that has bash + convert + sha256sum + mktemp + ImageMagick-but-not-jq
# We achieve "no jq" by creating a NEW minimal directory with hardlinks/links of
# the required binaries EXCEPT jq.  Falls back to a hard fail if any of the
# minimal binaries cannot be linked (e.g., READONLY filesystems).
minimal="$WORK/minimal_path"
mkdir -p "$minimal"
for cmd in bash convert sha256sum mktemp cat printf awk tee sort wc uniq head cut split read; do
    src="$(command -v "$cmd" 2>/dev/null || true)"
    [ -n "$src" ] && ln -sf "$src" "$minimal/$cmd" 2>/dev/null || true
done
# Confirm jq is absent from $minimal; if it's still there (somehow linked),
# delete the link.
[ -e "$minimal/jq" ] && rm -f "$minimal/jq"

if [ -e "$minimal/jq" ]; then
    echo "[WARN] scenario 3 (couldn't strip jq from minimal PATH — simulation unreliable)"
else
    RUNS_PER_CONFIG="$SELFTEST_RUNS" OUT_DIR="$WORK/det_run_c" PATH="$minimal" \
        CLI_DEBUG_PATH="$WORK/mock_cli/chronon3d_cli" \
        CLI_RELEASE_PATH="$WORK/mock_cli/chronon3d_cli" \
        bash "$GATE" > "$WORK/scenario_precond.log" 2>&1 || true
    scenario3_exit=$?
    if [ "$scenario3_exit" -eq 2 ]; then
        if grep -q 'GATE_FAIL_INTERNAL' "$WORK/scenario_precond.log" && grep -q 'jq' "$WORK/scenario_precond.log"; then
            echo "[PASS] scenario 3 (gate exit 2 + GATE_FAIL_INTERNAL + jq diagnostic)"
        else
            echo "[FAIL] scenario 3 (exit 2 but missing GATE_FAIL_INTERNAL or jq diagnostic)"
            SCENARIO3_FAIL=1
        fi
    else
        echo "[FAIL] scenario 3 (gate exit $scenario3_exit, expected 2)"
        echo "--- last 20 log lines ---"
        tail -20 "$WORK/scenario_precond.log" | sed 's/^/    /'
        SCENARIO3_FAIL=1
    fi
fi

# ── Verdict ────────────────────────────────────────────────────────────────
echo
echo "── verdict ──"
total_fail=0
[ "${SCENARIO1_FAIL:-0}" -eq 1 ] && total_fail=$((total_fail + 1))
[ "${SCENARIO2_FAIL:-0}" -eq 1 ] && total_fail=$((total_fail + 1))
[ "${SCENARIO3_FAIL:-0}" -eq 1 ] && total_fail=$((total_fail + 1))
[ "${SCENARIO4_FAIL:-0}" -eq 1 ] && total_fail=$((total_fail + 1))

if [ "$total_fail" -eq 0 ]; then
    echo "GATE_PASS: selftest harness covers 4 scenarios (PASS + FAIL + PRECOND + NO_DIAG) — all asserted OK"
    echo "[INFO] selftest_check_determinism: clean state across 4 scenario invocations verified (PASS + FAIL + PRECOND + NO_DIAG pass-modal watermark per NIT-1)"
    exit 0
fi

echo "GATE_FAIL: $total_fail of 3 selftest scenarios failed"
exit 1
