#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
#  AGENT 4 / TICKET-A4 — Verifica visuale e integrazione
#  tools/verify_cinematic_showcase.sh
#
#  Thin wrapper around the cinematic camera showcase test binary.  Locates
#  the binary in the active build tree, runs it via doctest (full
#  A4.1 / A4.2 / A4.3 / A4.4 / A4.5 / A4.6 gate surface), and reports
#  pass/fail per gate by grepping the doctest log for the canonical
#  "A4.X —" telemetry lines.
#
#  Determinism note: this script invokes the same binary twice if the
#  A4.4 determinism gate is enabled by the binary itself (the test
#  body runs the 6-frame loop twice internally).  This shell wrapper
#  invokes the binary ONCE and trusts its internal determinism check.
#
#  Exit code:
#    0   all six A4.* gates reported OK
#    1   any gate reported a failure OR the binary was not found OR
#        doctest exit code was non-zero
#
#  Optional env vars:
#    CHRONON3D_ROOT       project root (default: parent of this script)
#    CHRONON3D_BUILD_DIR  build tree root (default: ${CHRONON3D_ROOT}/build)
#    VERIFY_CTEST         if "1", use ctest -V instead of direct binary
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

CHRONON3D_ROOT="${CHRONON3D_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CHRONON3D_BUILD_DIR="${CHRONON3D_BUILD_DIR:-${CHRONON3D_ROOT}/build}"
VERIFY_CTEST="${VERIFY_CTEST:-0}"

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

# ── Decode the six DoD gates by grepping doctest output ──────────────
# Each gate prints its success line via MESSAGE("A4.X …") in the test
# body.  Match "A4.1 OK" / "A4.2 OK" / … / "A4.6 —" (A4.6 reports
# numeric telemetry only, so the marker is the bare "A4.6 —" prefix).
GATES=("A4.1 OK" "A4.2 OK" "A4.3 OK" "A4.4 OK" "A4.5 OK" "A4.6 ")
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

# Echo the A4.6 telemetry line verbatim (mean / total / rss).
echo
echo "VERIFY: A4.6 telemetry —"
grep -E 'A4\.6 (.|-|total=' "${LOG_PATH}" | head -n 1 || echo "  (no A4.6 line captured)"

# Optionally echo the contact sheet message so the user can see the path.
echo
echo "VERIFY: contact sheet —"
grep -E 'A4\.5 OK' "${LOG_PATH}" | head -n 1 || echo "  (no A4.5 line captured)"

echo
if [ "${ALL_OK}" = "1" ]; then
    echo "VERIFY_SHOWCASE: PASS  (all six A4.* gates OK)"
    exit 0
else
    echo "VERIFY_SHOWCASE: FAIL  (one or more A4.* gates missing — see ${LOG_PATH})"
    exit 1
fi
