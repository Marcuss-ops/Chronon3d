#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# test_check_ae_parity_golden_state.sh — Self-test for the ae_parity gate
# ═══════════════════════════════════════════════════════════════════════════
#
# Pure-bash Cat-2 regression lock for `tools/check_ae_parity_golden_state.sh`.
# Builds three synthetic AE_CAM golden fixtures under /tmp (a fresh-distinct
# set, a count-mismatch set, and a banned-hash-injection set) and verifies the
# gate's exit code + stdout GATE_PASS/GATE_FAIL/INTERNAL contract on each.
#
# Invocation
# ----------
#   bash tests/tools/test_check_ae_parity_golden_state.sh
#
# Exit codes
# ----------
#   0 ALL SELF-TEST CASES PASS
#   1 one or more self-test cases failed (gate logic regressed)
#
# Notes
# -----
# - Self-contained: does not require git, network, or the chronon3d build.
# - Deterministic: every fixture PNG is built from a deterministic byte stream
#   via `dd if=/dev/zero ... | tr ... > file` so sha256 is stable across runs.
# - Cat-2 freeze-allowed: test/* target idiom; mirrors `tests/visual/...` + 
#   `tests/render_graph/...` organization (top-level `tests/tools/` for
#   tool/gate self-tests; no CMakeLists registration needed for direct
#   invocation — pre-push hooks can call `bash tests/tools/test_check_*.sh`).
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
GATE="$REPO_ROOT/tools/check_ae_parity_golden_state.sh"

if [ ! -x "$GATE" ] && [ ! -r "$GATE" ]; then
    echo "✗ gate not found: $GATE"
    exit 1
fi

# ── Scratch dirs ─────────────────────────────────────────────────────────
SCRATCH="$(mktemp -d -t ae_parity_gate_test.XXXXXX)"
trap 'rm -rf "$SCRATCH"' EXIT

FAILS=0

# ── Helper: assert exit code + extract GATE_PASS/FAIL banner ─────────────
#
# Captures the inner command's exit code WITHOUT triggering `set -e` abort on
# a non-zero return.  The earlier `|| true` antipattern swalled the inner
# exit code via the compound-list semantics (rightmost sub-command wins).
# Now using a `set +e` toggle around the substitution to isolate the
# non-zero capture from the script-level `set -euo pipefail`.  Round-3 fix
# after self-test revealed `exit_code=0` always (assertion-by-bug).
assert_exit() {
    local want_exit="$1"; shift
    local want_banner_substr="$1"; shift
    local label="$1"; shift
    local out exit_code

    set +e
    out="$( "$@" 2>&1 )"
    exit_code=$?
    set -e

    if [ "$exit_code" -ne "$want_exit" ]; then
        echo "✗ [$label] exit=${exit_code} (want=${want_exit})"
        echo "    full output:"; printf '        %s\n' "$out" | head -20
        FAILS=$((FAILS + 1))
        return 1
    fi

    if ! echo "$out" | grep -q "$want_banner_substr"; then
        echo "✗ [$label] missing banner substring: '$want_banner_substr'"
        echo "    stdout head:"; echo "$out" | head -10 | sed 's/^/        /'
        FAILS=$((FAILS + 1))
        return 1
    fi

    echo "✓ [$label] exit=${exit_code}, contains '$want_banner_substr'"
    return 0
}

# ── Helper: build 24 synthetic PNG (each with distinct sha256) ───────────
build_fresh_set() {
    local dir="$1"
    mkdir -p "$dir"
    # 24 distinct-content files (deterministic sha256; each is 2 bytes long
    # so first 16 hex chars vary, also second-word varies so canonical 64-char
    # sha256 are guaranteed distinct).
    local i
    for i in $(seq 0 23); do
        printf '%s%02d' 'fresh' "$i" > "$dir/ae_cam_99_synthetic_frame${i}.png"
        # pad to 2 bytes minimum so file is non-empty.
    done
}

echo "=== Self-test: tools/check_ae_parity_golden_state.sh ==="
echo "scratch: $SCRATCH"
echo

# ── Test 1: 24 fresh-distinct PNG → expect exit 0 GATE_PASS ─────────────
T1="$SCRATCH/fresh_set"
build_fresh_set "$T1"
assert_exit 0 'GATE_PASS' \
    "Test 1: 24 fresh-distinct PNG exit 0 GATE_PASS" \
    env CHRONON3D_AE_PARITY_GOLDEN_DIR="$T1" bash "$GATE"

# ── Test 2: 23 PNG (count-mismatch) → expect exit 2 INTERNAL ────────────
T2="$SCRATCH/count_mismatch"
mkdir -p "$T2"
i=0; for i in $(seq 0 22); do
    printf 'mismatch%02d' "$i" > "$T2/ae_cam_99_synthetic_frame${i}.png"
done
assert_exit 2 'INTERNAL' \
    "Test 2: 23 PNG count mismatch exit 2 GATE_FAIL_INTERNAL" \
    env CHRONON3D_AE_PARITY_GOLDEN_DIR="$T2" bash "$GATE"

# ── Test 3: inject one in-tree fixture PNG with banned hash → exit 1 + filename ──
T3="$SCRATCH/banned_injection"
build_fresh_set "$T3"

# Reference the committed deterministic fixture (snapshot of the fc351bfe
# collision-encoded PNG, sha256 frozen).  Decouples the self-test from the
# mutating on-disk golden set so a future Soluzione B re-bake does NOT
# silently regress this assertion.  The fixture's own sha256 must remain
# `cc86d2b5e80287dc...` for Test 3 to be meaningful; verify it here.
BANNED_FIXTURE="$REPO_ROOT/tests/tools/fixtures/ae_cam_04_banned_fixture.png"
EXPECTED_HIT_FILE=""
FIXTURE_BANNED_SHA="cc86d2b5e80287dc62010b2da4d335500d41bf75f50e71b56c31af2c8195cc7a"

if [ -f "$BANNED_FIXTURE" ]; then
    FIXTURE_ACTUAL_SHA="$(sha256sum "$BANNED_FIXTURE" | awk '{print $1}')"
    if [ "$FIXTURE_ACTUAL_SHA" != "$FIXTURE_BANNED_SHA" ]; then
        echo "✗ [Test 3] in-tree fixture SHA256 drift: $FIXTURE_ACTUAL_SHA (expected $FIXTURE_BANNED_SHA)"
        echo "    FIX: re-snapshot the fixture from a known-banned source and re-commit."
        FAILS=$((FAILS + 1))
    else
        EXPECTED_HIT_FILE="ae_cam_04_banned_fixture.png"
        cp "$BANNED_FIXTURE" "$T3/$EXPECTED_HIT_FILE"
        # Trim synthetic set back to 24 total.
        rm -f "$T3/ae_cam_99_synthetic_frame23.png"
    fi
else
    echo "○ [Test 3] SKIPPED — fixture missing: $BANNED_FIXTURE"
fi

if [ -n "$EXPECTED_HIT_FILE" ]; then
    # Single capture: read output AND exit code from one invocation (avoids
    # the double-invocation antipattern of running the gate twice).
    out_and_exit="$(
        env CHRONON3D_AE_PARITY_GOLDEN_DIR="$T3" bash "$GATE" 2>&1
        echo "<<<EXIT>>>$?"
    )"
    gate_output="${out_and_exit%<<EXIT>>>*}"
    gate_output="${gate_output%<<<EXIT>>>*}"
    gate_exit="${out_and_exit##*<<<EXIT>>>}"

    if [ "$gate_exit" != "1" ]; then
        echo "✗ [Test 3] exit=${gate_exit} (want=1) for banned-injection set"
        echo "    output head:"; printf '        %s\n' "$gate_output" | head -10
        FAILS=$((FAILS + 1))
    elif echo "$gate_output" | grep -q "GATE_FAIL"; then
        if echo "$gate_output" | grep -q "$EXPECTED_HIT_FILE"; then
            echo "✓ [Test 3] exit=1 GATE_FAIL + banned filename '$EXPECTED_HIT_FILE' appears in diagnostic"
        else
            echo "✗ [Test 3] exit=1 GATE_FAIL but offending filename NOT in diagnostic"
            echo "    output head:"; printf '        %s\n' "$gate_output" | head -20
            FAILS=$((FAILS + 1))
        fi
    else
        echo "✗ [Test 3] exit=1 but GATE_FAIL banner missing on stdout"
        FAILS=$((FAILS + 1))
    fi
fi

echo
echo "=== Self-test summary ==="
if [ "$FAILS" -eq 0 ]; then
    echo "ALL PASS  (faILS=0)"
    exit 0
fi
echo "FAILED (faILS=$FAILS)"
exit 1
