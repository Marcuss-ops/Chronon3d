#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_ae_parity_golden_state.sh — Anti-stale-goldens GATE for ae_parity set
# ═══════════════════════════════════════════════════════════════════════════
#
# Asserts that the 24 AE_CAM golden PNGs in `tests/golden/ae_parity/` are
# *fresh-distinct* — i.e. NONE of them is still byte-identical to the
# `fc351bfe` collision-encoded workaround hash.
#
# Background
# ----------
# The ae_parity test suite (chronon3d_ae_parity_tests, gate #2 cat-2) verifies
# on-disk framebuffer-distinctness against goldens in tests/golden/ae_parity/.
# Historically a subset of AE_CAM scenes (02/03/04/05/07/09) collided
# (sha256 prefix `cc86d2b5e80287dc`); the `fc351bfe` workaround encoded that
# collision-hash into the on-disk goldens so the runtime CHECK could not
# surface the regression.
#
# The matrix-fix commit `c03ce2a2` shifted the collision OUT of the runtime
# FB-hash (memory), but the on-disk goldens remained collision-encoded for
# 13/24 PNG tracked.  A subsequent CHANGELOG claim "24 PNG re-baked" was
# `macchina-falsified` by sha256 observance (only AE_CAM_06 + AE_CAM_09_f015
# are actually fresh-distinct on disk).
#
# This gate prevents the recurring failure mode: a future cycle claiming
# "24 PNG re-baked" without per-PNG sha256 verification.  Every tracked PNG
# is checked against the known-banned full hash.  If ANY PNG matches the
# banned hash, the gate fails with an actionable diagnostic listing the
# offending filenames + (likely) the fix path (TICKET-ae-cam-hash-collision
# Soluzione B: `node_cache` camera-aware cache-key extension).
#
# Maint: if you add or remove an `ae_cam_*_*.png` golden (e.g. AE_CAM_11 with
# frame000/030/060), bump EXPECTED_PNG_COUNT at the line marked with the
# "<<< UPDATE HERE" comment below and re-run this gate to verify the new
# invariant.  PNG-count mismatch is `GATE_FAIL_INTERNAL` exit 2 by design —
# silent acceptance would let a drift hide.
#
# Behaviour
# ---------
#   - On success: stdout "GATE_PASS" + per-PNG PASS line; exit 0
#   - On failure (legacy collision-hash match): stdout "GATE_FAIL" + per-PNG
#     FAIL line; exit 1; on stderr: offending PNG filenames + forward-point
#     to TICKET-ae-cam-hash-collision Soluzione B
#   - On failure (directory / count precondition):
#     stdout "GATE_FAIL_INTERNAL" + exit 2
#   - Idempotent: safe for pre-push / CI / agent atomic-commit workflows.
#     Does NOT modify the working tree or index.
#
# Invocation
# ----------
#   bash tools/check_ae_parity_golden_state.sh
#   CHRONON3D_AE_PARITY_GOLDEN_DIR=path/to/alt bash tools/check_ae_parity_golden_state.sh
#
# Exit codes
# ----------
#   0 PASS — all 24 PNG fresh-distinct from the fc351bfe collision-encoded hash
#   1 FAIL — one or more PNG tracked still encode the fc351bfe hash
#   2 FAIL_INTERNAL — goldens directory missing or wrong number of PNG
#
# Cross-references
# ----------------
#   - tickets/TICKET-ae-cam-hash-collision.md  (Soluzione B — fix path)
#   - tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md  (Sub-cluster B forward-point)
#   - docs/CHANGELOG.md  (Phase G: c03ce2a2 matrix-fix + ae-parity re-bake)
#   - docs/FOLLOWUP_TICKETS.md  (rows 2 above + sha256 observance evidence)
#   - tools/check_repo_artifacts.sh  (sibling gate — anti-generated-files)
#   - tests/tools/test_check_ae_parity_golden_state.sh  (self-test fixture)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

# ── Configuration ───────────────────────────────────────────────────────
GOLDEN_DIR="${CHRONON3D_AE_PARITY_GOLDEN_DIR:-$REPO_ROOT/tests/golden/ae_parity}"

# Full SHA256 of the fc351bfe collision-encoded workaround hash (16-char
# prefix `cc86d2b5e80287dc`).  Computed once via:
#     sha256sum tests/golden/ae_parity/ae_cam_04_parent_null_frame000.png
# and verified against the 13/24 PNG tracked that share this exact digest.
BANNED_SHA256="cc86d2b5e80287dc62010b2da4d335500d41bf75f50e71b56c31af2c8195cc7a"

EXPECTED_PNG_COUNT=24  # <<< UPDATE HERE if AE_CAM scope expands (see header)

# ── Precondition: golden directory exists ───────────────────────────────
if [ ! -d "$GOLDEN_DIR" ]; then
    echo "GATE_FAIL_INTERNAL: golden directory missing: $GOLDEN_DIR" >&2
    echo "  → expected at <REPO_ROOT>/tests/golden/ae_parity/" >&2
    echo "GATE_FAIL (INTERNAL)"
    exit 2
fi

# ── Enumerate ae_cam_*_*.png (excluding .gitkeep / non-PNG siblings) ────
# Match AE_CAM golden naming pattern with `find` + `sort` (deterministic,
# port-friendly across GNU/BSD).  Excludes .gitkeep, the diff/ subdir, and
# any other files not matching the ae_cam_*_frameNNN.png canonical pattern.
mapfile -t GOLDEN_PNGS < <(
    find "$GOLDEN_DIR" -maxdepth 1 -type f -name 'ae_cam_*_*.png' \
        | sort
)

PNG_COUNT="${#GOLDEN_PNGS[@]}"

if [ "$PNG_COUNT" -ne "$EXPECTED_PNG_COUNT" ]; then
    cat >&2 <<EOF
GATE_FAIL_INTERNAL: expected $EXPECTED_PNG_COUNT ae_cam_*_*.png in $GOLDEN_DIR but found $PNG_COUNT
  → invariant violated: add/remove a golden with \`CHRONON3D_UPDATE_GOLDENS=1\` against
    the chronon3d_ae_parity_tests binary, then bump EXPECTED_PNG_COUNT
    (search "<<< UPDATE HERE") in this script accordingly.
    DO NOT silently swallow the discrepancy.
  → current contents of $GOLDEN_DIR (head 30):
EOF
    ls -1 "$GOLDEN_DIR" | head -30 | sed 's/^/    /' >&2
    echo "GATE_FAIL (INTERNAL)"
    exit 2
fi

# ── Per-PNG sha256 check vs. banned hash ────────────────────────────────
echo "=== AE_CAM golden freshness gate (post-c03ce2a2) ==="
echo "  banned hash: $BANNED_SHA256  (fc351bfe collision-encoded workaround)"
echo "  scanned:    $PNG_COUNT PNG in $GOLDEN_DIR"
echo

FAILED=0
BANNED_HITS=()

for png in "${GOLDEN_PNGS[@]}"; do
    base="$(basename "$png")"
    file_sha="$(sha256sum "$png" 2>/dev/null | awk '{print $1}')"

    if [ "$file_sha" = "$BANNED_SHA256" ]; then
        echo "  [STALE] $base  $file_sha"
        BANNED_HITS+=("$base")
        FAILED=1
    else
        echo "  [FRESH] $base  ${file_sha:0:16}..."
    fi
done

echo

# ── Verdict ──────────────────────────────────────────────────────────────
if [ "$FAILED" -ne 0 ]; then
    N_HITS="${#BANNED_HITS[@]}"
    echo "GATE_FAIL: ${N_HITS}/${PNG_COUNT} ae_cam_*_*.png still encode the fc351bfe collision-hash." >&2
    echo "  Conflicting files:" >&2
    for hit in "${BANNED_HITS[@]}"; do
        echo "    - $hit" >&2
    done >&2
    cat >&2 <<'FIX'
  → Forward-fix path: tickets/TICKET-ae-cam-hash-collision.md Soluzione B
    (extend `src/cache/node_cache.cpp::make_node_cache_key` with
    `camera_fingerprint = quantize(cam.zoom*1000) XOR
    quantize(cam.position.z*1000) XOR (parent.is_null ? 0 : hash(parent_id))`
    + propagate to 4 cache_key(ctx) sites + re-bake with
    `CHRONON3D_UPDATE_GOLDENS=1`).
  → NOT a re-bake-and-pray shortcut: subsequent re-bake MUST verify via
    THIS gate that no PNG re-encoded the banned hash.
FIX
    echo "GATE_FAIL"
    exit 1
fi

echo "GATE_PASS"
exit 0
