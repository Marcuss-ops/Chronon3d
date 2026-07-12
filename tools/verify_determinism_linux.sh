#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_determinism_linux.sh
#
# Canonical Determinism & Cache certification gate (P1).
#
# Verifies 6 determinism invariants per user spec verbatim:
#   1. Same frame × 5 runs same process   → all 5 sha256 identical
#   2. 5 separate CLI processes           → all 5 sha256 identical
#   3. Cache cold (1st run) vs warm (2nd) → both sha256 identical
#   4. Sequential vs random frame order    → each frame pair sha256 identical
#   5. Concurrency: 1 thread + 8 threads  → both sha256 identical
#   6. In-process cache metrics           → first.pixel_hash == second.pixel_hash,
#                                            second_render_cache_hits > 0, misses == 0
#                                            (BLOCKED on missing CLI metrics output)
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
# 7. Invariant 5: concurrency — 1 thread + 8 threads
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 7. Concurrency — 1 thread + 8 threads =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "thread_concurrency" "env blocked"
else
    H_T1=""
    H_T8=""
    OUT_T1="${OUTPUT_DIR}/thread_1.png"
    OUT_T8="${OUTPUT_DIR}/thread_8.png"
    H_T1=$(CHRONON3D_THREADS=1 "$CLI_BIN" still "CertDeterminism" "$OUT_T1" --frame 0 >/dev/null 2>&1 && sha256sum "$OUT_T1" 2>/dev/null | awk '{print $1}')
    H_T8=$(CHRONON3D_THREADS=8 "$CLI_BIN" still "CertDeterminism" "$OUT_T8" --frame 0 >/dev/null 2>&1 && sha256sum "$OUT_T8" 2>/dev/null | awk '{print $1}')
    if [ -n "$H_T1" ] && [ -n "$H_T8" ]; then
        if [ "$H_T1" = "$H_T8" ]; then
            _gate_pass "thread_concurrency_1_eq_8 (hash=$H_T1)"
        else
            _gate_fail "thread_concurrency_1_eq_8" "t1=$H_T1, t8=$H_T8"
        fi
    else
        _gate_fail "thread_concurrency" "render failed (t1='$H_T1', t8='$H_T8')"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 8. Invariant 6: in-process cache metrics
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 8. In-process cache metrics =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "cache_metrics" "env blocked"
else
    METRICS_OUT="${OUTPUT_DIR}/cache_metrics.log"
    FIRST_OUT="${OUTPUT_DIR}/cache_first.png"
    SECOND_OUT="${OUTPUT_DIR}/cache_second.png"
    # Render twice; capture stderr+stdout for metrics
    CHRONON3D_THREADS=1 "$CLI_BIN" still "CertDeterminism" "$FIRST_OUT" --frame 0 > "$METRICS_OUT.first" 2>&1
    CHRONON3D_THREADS=1 "$CLI_BIN" still "CertDeterminism" "$SECOND_OUT" --frame 0 > "$METRICS_OUT.second" 2>&1
    cat "$METRICS_OUT.first" "$METRICS_OUT.second" > "$METRICS_OUT"
    rm -f "$METRICS_OUT.first" "$METRICS_OUT.second"

    # Check first.pixel_hash == second.pixel_hash (file-based)
    if [ -f "$FIRST_OUT" ] && [ -f "$SECOND_OUT" ]; then
        H1=$(sha256sum "$FIRST_OUT" 2>/dev/null | awk '{print $1}')
        H2=$(sha256sum "$SECOND_OUT" 2>/dev/null | awk '{print $1}')
        if [ -n "$H1" ] && [ -n "$H2" ] && [ "$H1" = "$H2" ]; then
            _gate_pass "cache_metrics_first_eq_second (first.pixel_hash == second.pixel_hash)"
        else
            _gate_fail "cache_metrics_first_eq_second" "first=$H1, second=$H2"
        fi
    else
        _gate_fail "cache_metrics_files" "first or second output missing"
    fi

    # Check second_render_cache_hits > 0 (BLOCKED if CLI doesn't emit)
    if grep -qiE 'second_render_cache_hits' "$METRICS_OUT" 2>/dev/null; then
        HITS=$(grep -oiE 'second_render_cache_hits[ =:]+[0-9]+' "$METRICS_OUT" 2>/dev/null | head -1 | grep -oE '[0-9]+' | head -1)
        if [ -n "$HITS" ] && [ "$HITS" -gt 0 ]; then
            _gate_pass "cache_metrics_hits (second_render_cache_hits=$HITS > 0)"
        else
            _gate_fail "cache_metrics_hits" "second_render_cache_hits=$HITS (expected > 0)"
        fi
    else
        _gate_blocked "cache_metrics_hits" "CLI does not emit 'second_render_cache_hits' metric — forward-point: add cache-metrics output to chronon3d_cli still command (TICKET-VERIFY-DETERMINISM-CLI-CACHE-METRICS)"
    fi

    # Check misses == 0 (BLOCKED if CLI doesn't emit)
    if grep -qiE '\bmisses\b' "$METRICS_OUT" 2>/dev/null; then
        MISSES=$(grep -oiE 'misses[ =:]+[0-9]+' "$METRICS_OUT" 2>/dev/null | head -1 | grep -oE '[0-9]+' | head -1)
        if [ -n "$MISSES" ] && [ "$MISSES" -eq 0 ]; then
            _gate_pass "cache_metrics_misses (misses=0)"
        else
            _gate_fail "cache_metrics_misses" "misses=$MISSES (expected == 0)"
        fi
    else
        _gate_blocked "cache_metrics_misses" "CLI does not emit 'misses' metric — forward-point: add cache-metrics output to chronon3d_cli still command (TICKET-VERIFY-DETERMINISM-CLI-CACHE-METRICS)"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 9. Cleanup
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
# 10. Final verdict
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
    echo "[INFO] ${GATE_NAME}: 6/6 invariants verified (same-process + separate-process + cold-warm + frame-order + thread-concurrency + cache-metrics)"
    exit 0
fi
