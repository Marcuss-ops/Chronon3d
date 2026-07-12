#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_baseline_present.sh — TICKET-BASELINE-PRESENT gate
# ═══════════════════════════════════════════════════════════════════════════
#
# Forward-only enforcement of `docs/DOCUMENTATION_GOVERNANCE.md`
# §`docs/baselines/`:
#
#   "Le baseline sono prove immutabili di uno SHA di `main`. Ogni file
#    deve essere associato a un solo SHA e non deve essere aggiornato dopo
#    che `main` è avanzato."
#
#   Formato canonico: `main-<short-sha>-baseline.md` (8-char short SHA).
#
# Verifies that the current `HEAD` short SHA has a corresponding
# `docs/baselines/main-<sha>-baseline.md` file present.  This auto-detects
# when a new commit is pushed without a baseline snapshot, surfacing the
# gap at push time.  Parallel to the existing pre-push hygiene gates
# (4.5d/4.5e/4.5f/4.5g/4.5h in `tools/wrap_push.sh`):
#
#   - 4.5d `check_no_changelog_conflict_markers.sh` (CHANGELOG rot)
#   - 4.5e `check_text_golden_sources_aligned.sh`     (test source rot)
#   - 4.5f `check_doc_sha_dedup.sh`                   (ADR SHA-dedup rot)
#   - 4.5g `check_commit_subject_length.sh`          (subject length rot)
#   - 4.5h `check_no_source_conflict_markers.sh`      (source rot)
#   - 4.5i `check_baseline_present.sh` (this file)    (baseline rot)
#
# Exit codes: 0 = PASS (baseline present OR SKIP env var set),
#             1 = FAIL (no baseline for HEAD),
#             2 = internal script error.
#
# Bootstrap escape hatch:
#   CHRONON3D_SKIP_BASELINE_CHECK=true
#     ONE-TIME bootstrap only — the wire-up commit that introduces this
#     gate does not have a baseline for itself (the gate creation
#     precedes the baseline creation, by construction).  After the
#     wire-up lands, EVERY subsequent commit MUST have a corresponding
#     baseline snapshot or this gate will hard-block the push.  Documented
#     in the wire-up commit message + the gate script header.
#
# Usage:
#   bash tools/check_baseline_present.sh
#   CHRONON3D_SKIP_BASELINE_CHECK=true bash tools/check_baseline_present.sh  # bootstrap only
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME=check_baseline_present

# ── Bootstrap escape hatch (one-time) ─────────────────────────────────────
if [ "${CHRONON3D_SKIP_BASELINE_CHECK:-false}" = "true" ]; then
    echo "GATE_PASS: ${GATE_NAME} skipped (CHRONON3D_SKIP_BASELINE_CHECK=true, one-time bootstrap)"
    echo "[INFO] ${GATE_NAME}: skip-hatch active — wire-up bootstrap; next push must have a baseline"
    exit 0
fi

# ── Locate repo root + HEAD short SHA ────────────────────────────────────
REPO_ROOT="$(git rev-parse --show-toplevel)"
if [ -z "${REPO_ROOT}" ]; then
    echo "GATE_FAIL: ${GATE_NAME} — not in a git repository" >&2
    echo "GATE_FAIL" >&2
    exit 2
fi

HEAD_SHORT="$(git rev-parse --short=8 HEAD)"
if [ -z "${HEAD_SHORT}" ]; then
    echo "GATE_FAIL: ${GATE_NAME} — could not resolve HEAD short SHA" >&2
    echo "GATE_FAIL" >&2
    exit 2
fi

BASELINE_FILE="${REPO_ROOT}/docs/baselines/main-${HEAD_SHORT}-baseline.md"

# ── PASS: baseline present ───────────────────────────────────────────────
if [ -f "${BASELINE_FILE}" ]; then
    echo "GATE_PASS: ${GATE_NAME} — docs/baselines/main-${HEAD_SHORT}-baseline.md present"
    echo "[INFO] ${GATE_NAME}: HEAD ${HEAD_SHORT} has a corresponding baseline snapshot"
    exit 0
fi

# ── FAIL: baseline missing ───────────────────────────────────────────────
echo "GATE_FAIL: ${GATE_NAME} — docs/baselines/main-${HEAD_SHORT}-baseline.md MISSING for HEAD ${HEAD_SHORT}" >&2
echo "" >&2
echo "  Per docs/DOCUMENTATION_GOVERNANCE.md §docs/baselines/:" >&2
echo "    Every commit on main must have a corresponding baseline snapshot" >&2
echo "    named main-<short-sha>-baseline.md (8-char short SHA)." >&2
echo "    Baselines are immutable proofs of a specific main SHA." >&2
echo "" >&2
echo "  Remediation:" >&2
echo "    1. Create the baseline:" >&2
echo "         docs/baselines/main-${HEAD_SHORT}-baseline.md" >&2
echo "       Use one of the two canonical references as the template:" >&2
echo "         - green:    docs/baselines/main-7eb5c2ba-baseline.md" >&2
echo "         - rot-state: docs/baselines/main-df1e09d9-rot-cascade-baseline.md" >&2
echo "    2. Update docs/baselines/index.md (chronological TOC) per the" >&2
echo "       established maintenance contract (append-only row)." >&2
echo "    3. Prepend docs/CHANGELOG.md entry documenting the new baseline." >&2
echo "    4. Re-run: tools/wrap_push.sh origin main" >&2
echo "" >&2
echo "  One-time bootstrap escape hatch (NOT for routine use):" >&2
echo "         CHRONON3D_SKIP_BASELINE_CHECK=true tools/wrap_push.sh origin main" >&2
echo "       Use ONLY for the gate-introduction commit itself, where the" >&2
echo "       baseline for the new SHA does not yet exist by construction." >&2
echo "" >&2
echo "  Reference: docs/baselines/index.md (15 baselines, chronological TOC)." >&2
echo "GATE_FAIL" >&2
exit 1
