#!/usr/bin/env bash
# check_determinism_matrix.sh — Test #6 (5 esecuzioni, 1 hash).
# Render dello stesso frame 2x per ognuna delle 4 varianti (1_thread / all_cores /
# cache_cold / cache_warm). SHA256 + sort -u + wc -l per variante. PASS se
# sort -u | wc -l = 1 per OGNI variante; FAIL altrimenti.
# Per AGENTS.md §"INFO-level diagnostic style" emits [INFO] ${GATE_NAME}: ... additive
# addizionale al GATE_PASS canonico finale sul clean state.
# cache_cold: honor $CHRONON_CACHE_DIR override; otherwise try $HOME/.cache/chronon3d
# then $REPO_ROOT/build/.chronon-cache; emit one stderr note if no candidate was
# found so the variant label claims match reality (no silent degradation).
set -euo pipefail

GATE_NAME=check_determinism_matrix
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

COMPOSITION="${CHRONON_COMPOSITION:-ProductLaunch}"
FRAME="${CHRONON_FRAME:-60}"
OUT_DIR="${CHRONON_DETERMINISM_OUT:-/tmp/chronon-determinism-matrix}"
NPROC_VAL="$(nproc 2>/dev/null || echo 1)"

# ── Phase 0: binary presence check (canonical rebuild hint + §honesty) ──────
BINARY_CANDIDATES=(
    "$REPO_ROOT/build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli"
    "$REPO_ROOT/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli"
    "$REPO_ROOT/build/manual-test/apps/chronon3d_cli/chronon3d_cli"
)
CHRONON_CLI=""
for cand in "${BINARY_CANDIDATES[@]}"; do
    if [ -x "$cand" ]; then
        CHRONON_CLI="$cand"
        break
    fi
done

if [ -z "$CHRONON_CLI" ]; then
    echo "GATE_FAIL: chronon3d_cli binary not found in standard build paths" >&2
    echo "  checked: ${BINARY_CANDIDATES[*]}" >&2
    echo "  fix: cmake --preset linux-content-dev && cmake --build build/chronon/linux-content-dev -j\$(nproc)" >&2
    echo "  Per AGENTS.md §honesty: macchina-verifica deferred to working build host" >&2
    echo "  (this VPS lacks vcpkg glm/magic_enum + tmpfs quota for full project build)." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# Echo first-existing cache dir (CHRONON_CACHE_DIR override > common defaults > empty).
find_cache_target() {
    if [ -n "${CHRONON_CACHE_DIR:-}" ] && [ -d "${CHRONON_CACHE_DIR}" ]; then
        echo "${CHRONON_CACHE_DIR}"
    elif [ -d "$HOME/.cache/chronon3d" ]; then
        echo "$HOME/.cache/chronon3d"
    elif [ -d "$REPO_ROOT/build/.chronon-cache" ]; then
        echo "$REPO_ROOT/build/.chronon-cache"
    fi
}

# render 2x + confronta SHA256 ($1=label, $2=env_prefix word-list)
render_pair() {
    local label="$1" env_prefix="$2"
    local P1="$OUT_DIR/${label}_a.png" P2="$OUT_DIR/${label}_b.png"
    # shellcheck disable=SC2086  # env_prefix intentionally word-split for VAR=VAL assignments
    env $env_prefix "$CHRONON_CLI" still "$COMPOSITION" "$P1" --frame "$FRAME" \
        || { echo "  [${label}] render_a FAILED" >&2; return 2; }
    env $env_prefix "$CHRONON_CLI" still "$COMPOSITION" "$P2" --frame "$FRAME" \
        || { echo "  [${label}] render_b FAILED" >&2; return 2; }
    local H1 H2 N
    H1=$(sha256sum "$P1" | awk '{print $1}')
    H2=$(sha256sum "$P2" | awk '{print $1}')
    N=$(printf '%s\n%s\n' "$H1" "$H2" | sort -u | wc -l)
    if [ "$N" -eq 1 ]; then
        echo "  [${label}] deterministic sha256=${H1:0:12}…"
        return 0
    else
        echo "  [${label}] NON_DETERMINISTIC unique=${N} h_a=${H1:0:12} h_b=${H2:0:12}" >&2
        return 1
    fi
}

TOTAL_FAIL=0
render_pair "1_thread"  "CHRONON_THREADS=1"          || TOTAL_FAIL=$((TOTAL_FAIL+1))
render_pair "all_cores" "CHRONON_THREADS=$NPROC_VAL"  || TOTAL_FAIL=$((TOTAL_FAIL+1))

# cache_cold: flush a runtime cache dir if found; emit one note if none matched.
CACHE_TARGET="$(find_cache_target)"
if [ -n "$CACHE_TARGET" ]; then
    rm -rf "$CACHE_TARGET" 2>/dev/null || true
else
    echo "  cache_cold degraded to warm: no runtime cache dir found (set CHRONON_CACHE_DIR)" >&2
fi
render_pair "cache_cold" "" || TOTAL_FAIL=$((TOTAL_FAIL+1))

render_pair "cache_warm" "" || TOTAL_FAIL=$((TOTAL_FAIL+1))

if [ "$TOTAL_FAIL" -gt 0 ]; then
    echo "GATE_FAIL: $TOTAL_FAIL variant(s) non-deterministic in matrix (Test #6)" >&2
    echo "  Remediation: investigate thread/cache flakiness; check deterministic RNG seeding; verify single-frame cache reuse." >&2
    exit 1
fi

echo "GATE_PASS: all 4 variants deterministic — 1 unique hash per variant across 2 renders"
echo "[INFO] ${GATE_NAME}: zero non-deterministic variants (Test #6 determinism matrix holds)"
