#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
#  AGENT 4 / TICKET-A4 — Verifica visuale e integrazione
#  tools/verify_cinematic_showcase.sh
#
#  Thin wrapper around the cinematic camera showcase test binary.
#  Locates the binary in the active build tree, runs it via doctest, and
#  reports pass/fail per gate by grepping the doctest log for the
#  canonical "A4.X —" telemetry lines.
#
#  Runtime mode (Agent 2 / ci-showcase plan, Step 2/6):
#    --smoke              sets CHRONON3D_CINEMATIC_FRAME_COUNT=2 +
#                         CHRONON3D_CINEMATIC_COMP_COUNT=1 and lowers
#                         the required-gate set to
#                         { A4.1 OK , A4.2 OK , A4.3 OK (strict) ,
#                           A4.4 OK }
#                         no contact sheet (A4.5) and no perf envelope
#                         (A4.6) — DOCTEST_SKIP'd in the binary.
#    (no flag, default)   full mode: 6 frames × 5 compositions,
#                         required gates { A4.1 .. A4.6 }.
#
#  Exit code:
#    0   all required A4.* gates reported OK
#    1   any required gate missing OR the binary not found OR doctest
#        exit code non-zero
#
#  Optional env vars:
#    CHRONON3D_ROOT              project root (default: parent of this script)
#    CHRONON3D_BUILD_DIR         build tree root (default: ${CHRONON3D_ROOT}/build)
#    VERIFY_CTEST                if "1", use ctest -V instead of direct binary
#    CHRONON3D_CINEMATIC_FRAME_COUNT  forwarded to the binary (1..6)
#    CHRONON3D_CINEMATIC_COMP_COUNT   forwarded to the binary (1..5)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

CHRONON3D_ROOT="${CHRONON3D_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CHRONON3D_BUILD_DIR="${CHRONON3D_BUILD_DIR:-${CHRONON3D_ROOT}/build}"
VERIFY_CTEST="${VERIFY_CTEST:-0}"

# ── CLI flag parsing ──────────────────────────────────────────────────
SMOKE_MODE=0
while [ "${1:-}" != "" ]; do
  case "$1" in
    --smoke)
      SMOKE_MODE=1
      shift
      ;;
    -h|--help)
      echo "Usage: tools/verify_cinematic_showcase.sh [--smoke]"
      echo "  --smoke   : 2 frames × 1 composition, gates {A4.1, A4.2, A4.3 strict, A4.4}."
      echo "              No contact-sheet PNG, no A4.6 perf envelope. For daily push CI."
      echo "  (default) : 6 frames × 5 compositions, gates {A4.1..A4.6}."
      echo "              Contact-sheet PNG + A4.6 perf envelope. For nightly-visual workflow."
      exit 0
      ;;
    *)
      echo "VERIFY: unknown arg '$1' (try --help)" >&2
      exit 1
      ;;
  esac
done

# ── Forward env vars to the binary ────────────────────────────────────
# Default-preserving: only set if --smoke requested.  Otherwise pass
# through whatever the caller exported.
if [ "${SMOKE_MODE}" = "1" ]; then
  export CHRONON3D_CINEMATIC_FRAME_COUNT=2
  export CHRONON3D_CINEMATIC_COMP_COUNT=1
fi
echo "VERIFY: mode=$([[ \"${SMOKE_MODE}\" = \"1\" ]] && echo smoke || echo full) FRAME_COUNT=${CHRONON3D_CINEMATIC_FRAME_COUNT:-<default 6>} COMP_COUNT=${CHRONON3D_CINEMATIC_COMP_COUNT:-<default 5>}"

# ── Locate the showcase binary ────────────────────────────────────────
TEST_NAME="chronon3d_cinematic_camera_showcase_tests"
BIN_PATH=""
LOG_PATH="$(mktemp -t verify_showcase.XXXXXX.log)"

if [ "${VERIFY_CTEST}" = "1" ] && command -v ctest >/dev/null 2>&1; then
    echo "VERIFY: using ctest -V"
    cd "${CHRONON3D_ROOT}"
    if ctest --test-dir "${CHRONON3D_BUILD_DIR}" \
             -R "^${TEST_NAME}$" -V > "${LOG_PATH}" 2>&1; then
        RC=0
    else
        RC=$?
    fi
else
    # Search the build tree for the binary.  cmake / ninja usually park
    # test executables at top-level build/<test_name> when no per-area
    # subdir is configured.  Fall back to a deep find if the obvious
    # location misses.
    if [ -x "${CHRONON3D_BUILD_DIR}/${TEST_NAME}" ]; then
        BIN_PATH="${CHRONON3D_BUILD_DIR}/${TEST_NAME}"
    else
        BIN_PATH="$(find "${CHRONON3D_BUILD_DIR}" \
                       -name "${TEST_NAME}" -type f -executable \
                       -print -quit 2>/dev/null || true)"
    fi

    if [ -z "${BIN_PATH}" ]; then
        echo "VERIFY: FAIL — could not locate ${TEST_NAME} under ${CHRONON3D_BUILD_DIR}"
        echo "  Build the showcase target first:"
        echo "    cmake --build ${CHRONON3D_BUILD_DIR} --target ${TEST_NAME}"
        exit 1
    fi

    echo "VERIFY: binary ${BIN_PATH}"
    cd "${CHRONON3D_ROOT}"
    if "${BIN_PATH}" > "${LOG_PATH}" 2>&1; then
        RC=0
    else
        RC=$?
    fi
fi

echo "VERIFY: log ${LOG_PATH}"
echo

# ── Required-gate list per run-mode ─────────────────────────────────
# Smoke: A4.5 (contact-sheet PNG) + A4.6 (perf envelope) are
# DOCTEST_SKIP'd in the binary, so their marker substrings are NOT in
# the log.  Listing them as required gates would force FAIL.  A4.3 also
# runs with 1 preset (instead of 5), but its "A4.3 OK (per-preset
# strict" marker is the same regardless of preset count, so the
# substring match is stable across modes.
#
# Full: all six gates required.  Backwards-compatible with the historical
# behaviour before Step 2/6 of the Agent 2 plan.
if [ "${SMOKE_MODE}" = "1" ]; then
    # Smoke: 1 comp × 2 frames.  A4.1 OK + A4.2 OK + A4.3 strict + A4.4 OK.
    GATES=("A4.1 OK" "A4.2 OK" "A4.3 OK (per-preset strict" "A4.4 OK")
else
    # Full: 5 comps × 6 frames.  All six gates.  The trailing space in
    # "A4.6 " is intentional — A4.6 emits "A4.6 OK — ..." in MESSAGE.
    GATES=("A4.1 OK" "A4.2 OK" "A4.3 OK (per-preset strict" "A4.4 OK" "A4.5 OK" "A4.6 ")
fi
ALL_OK=1

for gate in "${GATES[@]}"; do
    if grep -q -F "${gate}" "${LOG_PATH}"; then
        printf '  ✓ %s\n' "${gate}"
    else
        printf '  ✗ %s   (MISSING)\n' "${gate}"
        ALL_OK=0
    fi
done
echo

# ── Report determinism + main binary status ──────────────────────────
if [ "${RC}" -eq 0 ]; then
    echo "VERIFY: doctest exit OK (rc=0)"
else
    echo "VERIFY: doctest exit FAILED (rc=${RC})"
    ALL_OK=0
fi

# Echo the A4.6 telemetry line verbatim (full mode only).  In smoke
# mode the binary's A4.6 is DOCTEST_SKIP'd so the line isn't emitted
# and we don't want a noisy "(no A4.6 line captured)" footer.
if [ "${SMOKE_MODE}" = "0" ]; then
    echo
    echo "VERIFY: A4.6 telemetry —"
    grep -E 'A4\.6 (.|-|total=' "${LOG_PATH}" | head -n 1 || echo "  (no A4.6 line captured)"

    echo
    echo "VERIFY: contact sheet —"
    grep -E 'A4\.5 OK' "${LOG_PATH}" | head -n 1 || echo "  (no A4.5 line captured)"
fi

echo
if [ "${ALL_OK}" = "1" ]; then
    if [ "${SMOKE_MODE}" = "1" ]; then
        echo "VERIFY_SHOWCASE: PASS  (smoke: 1 comp × 2 frames; required gates A4.1 OK / A4.2 OK / A4.3 OK strict / A4.4 OK)"
    else
        echo "VERIFY_SHOWCASE: PASS  (full: 5 presets × 6 frames; required gates A4.1 OK .. A4.6 OK)"
    fi
    exit 0
else
    echo "VERIFY_SHOWCASE: FAIL  (one or more required gates missing — see ${LOG_PATH})"
    exit 1
fi
