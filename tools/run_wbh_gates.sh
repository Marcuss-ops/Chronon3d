#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# run_wbh_gates.sh — Canonical WBH (Working Build Host) gate chain
# ═══════════════════════════════════════════════════════════════════════════
#
# Single source of truth for the WBH gate chain run on a Working Build
# Host before any `CHRONON3D_GATE_PROFILE=wbh` push is allowed.  Invoked
# by `tools/wrap_push.sh` (case `wbh)` branch — see `tools/wrap_push.sh`
# for the dispatch flow), and standalone via `bash tools/run_wbh_gates.sh`
# for operator smoke verification.
#
# Gate chain: sourced from `tools/gates/manifest.sh`::WBH_ONLY_GATES
# (canonical, single source of truth).  Per AGENTS.md Cat-3
# anti-duplication discipline, the gate list lives ONLY in
# `manifest.sh`, NOT inline here.  Current canonical count: 8 gates.
#
# User-spec vs canonical manifest:
#   The user's `chore(tools)` spec listed 7 gates
#     (check_video_completeness + check_glow_certification +
#      check_determinism + check_determinism_matrix +
#      check_batch_100_videos + verify_sdk_consumer +
#      manual_touches_per_video)
#   The canonical `WBH_ONLY_GATES` defines 8 gates; the extra gate
#   is `check_fix_cronograph.sh`.  Per the spec's "NON eliminare i
#   gate verificatori — solo la loro duplicazione inline nel wrapper"
#   clause, removing any verifier from the canonical chain would
#   violate the rule.  This chore therefore runs the canonical 8.
#   Manifest-vs-spec drift is forward-pointed to
#   `TICKET-GATES-MANIFEST-DRIFT` (separate ticket — not in this
#   chore scope per "Fare PR piccole e mirate").
#
# Exit code semantics (canonical 3-state envelope used by all
# tools/check_*.sh):
#   0 = PASS              — chain finished, all gates emitted success.
#   1 = FAIL              — first failed gate aborts the chain.
#   2 = INTERNAL_ERROR    — gate script missing / non-executable / spawn error.
#
# Methodology: sequential, fail-fast.  The chain aborts on the first
# `GATE_FAIL`, mirroring the canonical test-suite convention to surface
# the root-cause gate early instead of burying it under N downstream
# errors (per AGENTS.md `### Disciplina di aggiornamento dei canonici`
# minimal-noise discipline).
#
# Honest-limitation disclosure (per AGENTS.md `## Regole` §honesty):
# This chain cannot PASS on a non-build-host VPS (no `chronon3d_cli`
# linker + no `ffmpeg` binary + tmpfs quota + pre-existing test rot
# patterns).  Verification end-to-end DEFERRED-WBH per the established
# TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent +
# macchina-verifica-puntiforme pattern in `docs/DEFINITION_OF_DONE.md`.
# macchina-verifica on a Working Build Host will exercise this chain
# as `bash tools/run_wbh_gates.sh` and asserting exit 0 + 8/8
# `GATE_PASS` lines.
#
# Usage:
#   bash tools/run_wbh_gates.sh
#   CHRONON3D_GATE_PROFILE=wbh bash tools/wrap_push.sh origin main
#   bash tools/run_wbh_gates.sh && echo OK || echo FAIL
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME="run_wbh_gates"
REPO_ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="${REPO_ROOT}/tools"

# ── Canonical WBH gate chain (sourced from manifest; single source of truth) ──
# Per AGENTS.md Cat-3 anti-duplication, the gate list lives in
# `tools/gates/manifest.sh`::WBH_ONLY_GATES, NOT inline here.  Adding
# or removing a gate = edit `manifest.sh` ONLY; this file stays the
# same.  Forward-point `TICKET-GATES-MANIFEST-DRIFT` documents the
# user's 7 spec vs manifest's 8 canonical list.
# shellcheck source=gates/manifest.sh
source "${SCRIPT_DIR}/gates/manifest.sh"

GATE_COUNT=${#WBH_ONLY_GATES[@]}
echo "${GATE_NAME}: starting WBH gate chain (${GATE_COUNT} gates, sourced from gates/manifest.sh)..."

idx=0
for gate in "${WBH_ONLY_GATES[@]}"; do
    idx=$((idx + 1))
    if [ ! -x "${SCRIPT_DIR}/${gate}" ]; then
        if [ ! -f "${SCRIPT_DIR}/${gate}" ]; then
            echo "GATE_FAIL_INTERNAL: ${gate} missing (path: ${SCRIPT_DIR}/${gate})" >&2
            exit 2
        fi
        # File exists but not executable — fail-loud (PASS-by-default is
        # forbidden per AGENTS.md "non sorprendere l'utente") + surface
        # the canonical chmod +x remediation hint.
        echo "GATE_FAIL_INTERNAL: ${gate} not executable (path: ${SCRIPT_DIR}/${gate})" >&2
        echo "  fix: chmod +x tools/${gate}" >&2
        exit 2
    fi
    echo "  [${idx}/${GATE_COUNT}] ${gate}"
    if ! bash "${SCRIPT_DIR}/${gate}"; then
        rc=$?
        echo "GATE_FAIL: ${gate} (exit $rc)" >&2
        exit 1
    fi
done

echo "GATE_PASS: ${GATE_COUNT}/${GATE_COUNT} WBH gates PASSED"
# AGENTS.md `## Regole di lint documentale` `### INFO-level diagnostic style` rule:
# one-liner addizionale al canonico GATE_PASS, grep-discoverable via [INFO] prefix + gate-name self-id.
echo "[INFO] ${GATE_NAME}: ${GATE_COUNT}/${GATE_COUNT} WBH verification gates executed successfully in sequence (sourced from gates/manifest.sh)"
exit 0
