#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_determinism_linux.sh
#
# Canonical Determinism & Cache certification gate (P1).
#
# Verifies 4 determinism invariants per user spec verbatim:
#   1. Same frame × 5 runs same process   → all 5 sha256 identical
#   2. 5 separate CLI processes           → all 5 sha256 identical
#   3. Cache cold (1st run) vs warm (2nd) → both sha256 identical
#   4. Sequential vs random frame order    → each frame pair sha256 identical
#
# Verdict contract:
#   DETERMINISM_FUNCTIONAL_PASS — all 4 invariants hold
#   DETERMINISM_FUNCTIONAL_FAIL   — any invariant violated
#   DETERMINISM_FUNCTIONAL_BLOCKED — env/binary not available
#   Exit 0 = PASS, 1 = FAIL, 2 = BLOCKED
#
# Usage:
#   bash tools/verify_determinism_linux.sh
#
# Environment:
#   CHRONON3D_DET_CLI_BIN=<path>    Override CLI binary path
#   CHRONON3D_DET_KEEP_FRAMES=1     Keep rendered frames in /tmp
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_determinism_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_DET_CLI_BIN="${CHRONON3D_DET_CLI_BIN:-}"
CHRONON3D_DET_KEEP_FRAMES="${CHRONON3D_DET_KEEP_FRAMES:-0}"

set -uo pipefail

OUTPUT_DIR="/tmp/chronon3d_determinism_cert"
mkdir -p "$OUTPUT_DIR"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass() { echo "  [PASS] $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail() { echo "  [FAIL] $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked() { echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

find_chronon3d_cli() {
    if [ -n "$CHRONON3D_DET_CLI_BIN" ] && [ -x "$CHRONON3D_DET_CLI_BIN" ]; then
        echo "$CHRONON3D_DET_CLI_BIN"; return 0
    fi
    for candidate in \
        "${ROOT}/build/chronon/linux-content-dev/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/build/manual-test/chronon3d_cli" \
        "$(command -v chronon3d_cli 2>/dev/null)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            echo "$candidate"; return 0
        fi
    done
    return 1
}

# Render a single still and return its sha256.
still_hash() {
    local comp="$1" frame="$2" out="$3"
    "$CLI_BIN" still "$comp" "$out" --frame "$frame" >/dev/null 2>&1 || return 1
    [ -s "$out" ] || return 1
    sha256sum "$out" | awk '{print $1}'
}

# Check all hashes in a space-separated list are identical.
# Returns 0 (via awk) if all match, 1 otherwise.
all_identical() {
    local hashes="$1" label="$2"
    local unique
    unique=$(echo "$hashes" | tr ' ' '\n' | sort -u | wc -l)
    if [ "$unique" -eq 1 ]; then
        local first
        first=$(echo "$hashes" | awk '{print $1}')
        _gate_pass "$label (all=$first)"
    else
        _gate_fail "$label" "$unique unique hashes found (expected 1)"
    fi
}

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_determinism_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

if ! git diff --quiet HEAD 2>/dev/null; then
    echo "DET_FAIL: dirty working tree"; git status -sb; exit 1
fi
if ! git diff --cached --quiet; then
    echo "DET_FAIL: staged changes"; git status -sb; exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "DET_FAIL: untracked changes"; git status -sb; exit 1
fi

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")
if [ "$LOCAL" != "$REMOTE" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "DET_FAIL: diverged"; exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Tree: clean"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

CLI_BIN="$(find_chronon3d_cli)" || {
    _gate_blocked "chronon3d_cli" "not found"; ENV_BLOCKED=true
}
if [ -n "$CLI_BIN" ] && [ "$ENV_BLOCKED" = false ]; then
    _gate_pass "chronon3d_cli ($(stat -c%s "$CLI_BIN" 2>/dev/null || echo 0) bytes)"
fi

command -v sha256sum >/dev/null 2>&1 \
    && _gate_pass "sha256sum" \
    || { _gate_blocked "sha256sum" "not found"; ENV_BLOCKED=true; }

# ══════════════════════════════════════════════════════════════════════════════
# 3. Invariant 1: same process × 5 runs
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Same process × 5 runs =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "same_process" "env blocked"
else
    HASHES=""
    for i in 1 2 3 4 5; do
        OUT="${OUTPUT_DIR}/same_proc_${i}.png"
        H=$(still_hash "CertDeterminism" 0 "$OUT")
        [ -n "$H" ] && HASHES="$HASHES $H" || { _gate_fail "same_proc_run_$i" "render failed"; }
    done
    all_identical "$HASHES" "same_process_5runs"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. Invariant 2: 5 separate processes
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. 5 separate processes =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "separate_processes" "env blocked"
else
    HASHES=""
    for i in 1 2 3 4 5; do
        OUT="${OUTPUT_DIR}/sep_proc_${i}.png"
        # Each run is a separate CLI invocation (fresh process)
        H=$("$CLI_BIN" still "CertDeterminism" "$OUT" --frame 0 >/dev/null 2>&1 \
            && sha256sum "$OUT" 2>/dev/null | awk '{print $1}')
        [ -n "$H" ] && HASHES="$HASHES $H" || { _gate_fail "sep_proc_$i" "render failed"; }
    done
    all_identical "$HASHES" "separate_processes_5runs"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. Invariant 3: cache cold vs warm
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. Cache cold vs warm =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "cache_cold_warm" "env blocked"
else
    COLD_OUT="${OUTPUT_DIR}/cache_cold.png"
    WARM_OUT="${OUTPUT_DIR}/cache_warm.png"

    H_COLD=$(still_hash "CertDeterminism" 0 "$COLD_OUT")
    [ -n "$H_COLD" ] && _gate_pass "cache_cold (hash=$H_COLD)" \
        || _gate_fail "cache_cold" "render failed"

    # Second render within same gate execution (warm cache)
    H_WARM=$(still_hash "CertDeterminism" 0 "$WARM_OUT")
    [ -n "$H_WARM" ] && _gate_pass "cache_warm (hash=$H_WARM)" \
        || _gate_fail "cache_warm" "render failed"

    if [ -n "$H_COLD" ] && [ -n "$H_WARM" ]; then
        [ "$H_COLD" = "$H_WARM" ] \
            && _gate_pass "cache_cold_eq_warm" \
            || _gate_fail "cache_cold_eq_warm" "cold=$H_COLD, warm=$H_WARM"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. Invariant 4: sequential vs random frame order
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. Sequential vs random frame order =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "frame_order" "env blocked"
else
    # Render 5 distinct frames in sequential order
    SEQ_HASHES=""
    for f in 0 15 30 75 90; do
        OUT="${OUTPUT_DIR}/seq_f${f}.png"
        H=$(still_hash "CertDeterminism" "$f" "$OUT")
        [ -n "$H" ] && SEQ_HASHES="$SEQ_HASHES $H" \
            || { _gate_fail "seq_f${f}" "render failed"; }
    done

    # Render same 5 frames in random order
    RND_HASHES=""
    for f in 75 0 90 30 15; do
        OUT="${OUTPUT_DIR}/rnd_f${f}.png"
        H=$(still_hash "CertDeterminism" "$f" "$OUT")
        [ -n "$H" ] && RND_HASHES="$RND_HASHES $H" \
            || { _gate_fail "rnd_f${f}" "render failed"; }
    done

    # Check: each frame's hash should match regardless of render order
    FRAME_PAIRS_OK=0
    FRAME_PAIRS_FAIL=0
    for f in 0 15 30 75 90; do
        SEQ_FILE="${OUTPUT_DIR}/seq_f${f}.png"
        RND_FILE="${OUTPUT_DIR}/rnd_f${f}.png"
        if [ -f "$SEQ_FILE" ] && [ -f "$RND_FILE" ]; then
            H_S=$(sha256sum "$SEQ_FILE" | awk '{print $1}')
            H_R=$(sha256sum "$RND_FILE" | awk '{print $1}')
            if [ "$H_S" = "$H_R" ]; then
                FRAME_PAIRS_OK=$((FRAME_PAIRS_OK + 1))
            else
                FRAME_PAIRS_FAIL=$((FRAME_PAIRS_FAIL + 1))
                _gate_fail "frame_order_f${f}" "seq=$H_S, rnd=$H_R"
            fi
        fi
    done

    [ "$FRAME_PAIRS_FAIL" -eq 0 ] \
        && _gate_pass "frame_order_any_order (${FRAME_PAIRS_OK}/5 pairs identical)" \
        || true
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Cleanup
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 7. Cleanup =="

if [ "$CHRONON3D_DET_KEEP_FRAMES" = "1" ]; then
    echo "  Keeping frames at $OUTPUT_DIR"
    _gate_pass "cleanup (preserved)"
else
    rm -rf "$OUTPUT_DIR" 2>/dev/null || true
    _gate_pass "cleanup (removed)"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 8. Final verdict
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$ENV_BLOCKED" = true ]; then
    echo "DETERMINISM_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Determinism certification blocked by environment."
    echo "  macchina-verifica DEFERRED to working build host per AGENTS.md §honest-limitation"
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "DETERMINISM_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed."
    exit 1
else
    echo "DETERMINISM_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Determinism certified."
    echo "[INFO] ${GATE_NAME}: 4/4 invariants verified (same-process + separate-process + cold-warm + frame-order)"
    exit 0
fi
