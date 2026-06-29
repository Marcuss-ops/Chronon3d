#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
#  AGENT 4 / TICKET-A4 — Verifica visuale e integrazione
#  tools/render_showcase_contact_sheet.sh
#
#  Companion to tools/verify_cinematic_showcase.sh.  Where the verify
#  script runs all six DoD gates, this script is laser-focused on the
#  A4.5 contact sheet — invokes the showcase test binary so the A4.5
#  gate composes output/showcase/contact_sheet.png, then asserts the
#  PNG exists, is non-empty, and reports its byte size + resolution.
#
#  Runtime mode (Agent 2 / ci-showcase plan, Step 2/6):
#    --smoke              sets CHRONON3D_CINEMATIC_FRAME_COUNT=2.
#                         The binary's A4.5 gate is DOCTEST_SKIP'd
#                         and no PNG is produced.  This script exits
#                         0 immediately so daily CI stays
#                         artefact-free.
#    (default)            full mode: 6 frames × 5 compositions → 5760×2160
#                         master contact-sheet PNG + dims/secs reported.
#
#  Usage:
#    tools/render_showcase_contact_sheet.sh                 # render + validate
#    tools/render_showcase_contact_sheet.sh --open          # also xdg-open on
#                                                          # the local box
#    tools/render_showcase_contact_sheet.sh --smoke         # short-circuit
#    tools/render_showcase_contact_sheet.sh --smoke --open  # short-circuit
#
#  Optional env vars:
#    CHRONON3D_ROOT              project root (default: parent of this script)
#    CHRONON3D_BUILD_DIR         build tree root (default: build/)
#    CHRONON3D_CINEMATIC_FRAME_COUNT  forwarded to the binary (1..6)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

CHRONON3D_ROOT="${CHRONON3D_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CHRONON3D_BUILD_DIR="${CHRONON3D_BUILD_DIR:-${CHRONON3D_ROOT}/build}"
TEST_NAME="chronon3d_cinematic_camera_showcase_tests"
OUT_DIR="${CHRONON3D_ROOT}/output/showcase"
SHEET_PATH="${OUT_DIR}/contact_sheet.png"

# ── CLI flag parsing ──────────────────────────────────────────────────
DO_OPEN=0
SMOKE_MODE=0
for arg in "$@"; do
  case "${arg}" in
    --open) DO_OPEN=1 ;;
    --smoke)
      SMOKE_MODE=1
      export CHRONON3D_CINEMATIC_FRAME_COUNT=2
      ;;
  esac
done

# ── Locate the showcase binary (needed for both smoke + full) ─────────
BIN_PATH=""
if [ -x "${CHRONON3D_BUILD_DIR}/${TEST_NAME}" ]; then
    BIN_PATH="${CHRONON3D_BUILD_DIR}/${TEST_NAME}"
else
    BIN_PATH="$(find "${CHRONON3D_BUILD_DIR}" \
                   -name "${TEST_NAME}" -type f -executable \
                   -print -quit 2>/dev/null || true)"
fi

if [ -z "${BIN_PATH}" ]; then
    echo "CONTACT_SHEET: FAIL — could not locate ${TEST_NAME} under ${CHRONON3D_BUILD_DIR}"
    echo "  Build the showcase target first:"
    echo "    cmake --build ${CHRONON3D_BUILD_DIR} --target ${TEST_NAME}"
    exit 1
fi

echo "CONTACT_SHEET: binary ${BIN_PATH}"

# ── Smoke: short-circuit BEFORE invoking the binary ──────────────────
# In smoke mode the binary's A4.5 gate is DOCTEST_SKIP'd and no PNG
# is produced.  We assert that explicitly here so the caller gets a
# clear PASS-with-skip message instead of a confusing "(file not
# written)" FAIL.  Contact-sheet rendering is a nightly concern only.
if [ "${SMOKE_MODE}" = "1" ]; then
    echo "CONTACT_SHEET: SKIPPED — contact sheet is a nightly / full-mode artefact."
    echo "  (smoke: CHRONON3D_CINEMATIC_FRAME_COUNT=2; A4.5 DOCTEST_SKIP'd in binary)"
    echo "  Re-run without --smoke against .github/workflows/nightly-visual.yml"
    echo "  to render the full 5760×2160 grid."
    exit 0
fi

# ── Run the showcase binary; the A4.5 gate composes the PNG ──────────
cd "${CHRONON3D_ROOT}"
LOG_PATH="$(mktemp -t contact_sheet.XXXXXX.log)"
if "${BIN_PATH}" > "${LOG_PATH}" 2>&1; then
    RC_BIN=0
else
    RC_BIN=$?
    echo "CONTACT_SHEET: binary exited with rc=${RC_BIN} (continuing to file checks)"
fi

# ── Assert the PNG exists, is non-empty, sanity-check the file ────────
if [ ! -f "${SHEET_PATH}" ]; then
    echo "CONTACT_SHEET: FAIL — ${SHEET_PATH} was not written"
    echo "  Last lines from binary log:"
    tail -n 20 "${LOG_PATH}" | sed 's/^/    /'
    exit 1
fi

BYTES="$(stat -c '%s' "${SHEET_PATH}" 2>/dev/null || stat -f '%z' "${SHEET_PATH}")"
if [ "${BYTES}" -le 0 ]; then
    echo "CONTACT_SHEET: FAIL — ${SHEET_PATH} is empty (0 bytes)"
    exit 1
fi

# ── Decode PNG header → width × height ─────────────────────────────────
# The PNG IHDR chunk is the first 24 bytes after the 8-byte signature.
# Width is bytes 16-19, height is bytes 20-23 (big-endian).
# Portable parser ladder: python3 (preferred), xxd, host-byte-order od.
# BSD od on macOS lacks `--endian=big`; `xxd -p` is BSD+GNU-portable;
# `python3` is universally available on CI runners.  Try them in order.
PNG_DIM=""
# PNG IHDR parser ladder: python3 -c (single-line, single-quoted →
# no heredoc) → xxd → od.  We use single-quoted `-c '...'` so the
# inner double-quoted Python string is preserved verbatim; python
# reads the file path from argv.  The `\u00d7` is interpreted by
# Python's f-string (not by bash), rendering as `×`.
if command -v python3 >/dev/null 2>&1; then
    PNG_DIM="$(python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read()[16:24]; w,h=struct.unpack(">II", d); print(f"{w} \u00d7 {h}")' "${SHEET_PATH}" 2>/dev/null || true)"
fi
if [ -z "${PNG_DIM}" ] && command -v xxd >/dev/null 2>&1; then
    xxd_out="$(head -c 24 "${SHEET_PATH}" 2>/dev/null | xxd -p 2>/dev/null || true)"
    # Bytes 16-19 (width) and 20-23 (height) are at offsets 32-39 and 40-47
    # in the hex dump (each byte -> 2 hex chars).
    if [ -n "${xxd_out}" ]; then
        w_hex="${xxd_out:32:8}"
        h_hex="${xxd_out:40:8}"
        if [ -n "${w_hex}" ] && [ -n "${h_hex}" ]; then
            PNG_DIM="$((16#${w_hex})) × $((16#${h_hex}))"
        fi
    fi
fi
if [ -z "${PNG_DIM}" ] && command -v od >/dev/null 2>&1; then
    # Final fallback: 4-byte reads via od (host byte order).
    # On little-endian host, width then height both still fit 32 bits so
    # a literal swap gives correct values.  On big-endian host, swap to
    # correct order.
    od_out="$(head -c 24 "${SHEET_PATH}" 2>/dev/null | tail -c 8 2>/dev/null | od -An -tu4 -N8 2>/dev/null || true)"
    PNG_W_TMP="$(echo "${od_out}" | awk 'NR==1 {print $1}')"
    PNG_H_TMP="$(echo "${od_out}" | awk 'NR==2 {print $2}')"
    if [ -n "${PNG_W_TMP:-}" ] && [ -n "${PNG_H_TMP:-}" ]; then
        PNG_DIM="${PNG_W_TMP} × ${PNG_H_TMP}"
    fi
fi
if [ -z "${PNG_DIM}" ]; then
    PNG_DIM="(unable to parse PNG header in this env)"
fi

echo "CONTACT_SHEET: OK"
echo "  path:    ${SHEET_PATH}"
echo "  size:    ${BYTES} bytes"
echo "  dims:    ${PNG_DIM}"
echo "  layout:  3 × 2 grid of 1920 × 1080 frames"
echo "          (master composite per the A4.5 test body)"
echo

# ── Print A5.4-style banner if interactive + xdg-open available ──────
if [ "${DO_OPEN}" = "1" ]; then
    if command -v xdg-open >/dev/null 2>&1; then
        echo "CONTACT_SHEET: launching xdg-open ${SHEET_PATH}"
        xdg-open "${SHEET_PATH}" >/dev/null 2>&1 || true
    elif command -v open >/dev/null 2>&1; then
        echo "CONTACT_SHEET: launching open ${SHEET_PATH}"
        open "${SHEET_PATH}" >/dev/null 2>&1 || true
    else
        echo "CONTACT_SHEET: no xdg-open / open available; skipping visual launch"
    fi
fi

# Also echo the A4.5 line from the binary log if present.
echo "CONTACT_SHEET: doctest A4.5 line —"
grep -E 'A4\.5 OK' "${LOG_PATH}" | head -n 1 || echo "  (no A4.5 line captured)"

# Always surface the binary's rc if non-zero (the file checks above
# already proved the contact sheet was written regardless).
if [ "${RC_BIN}" -ne 0 ]; then
    echo "CONTACT_SHEET: NOTE — binary rc=${RC_BIN}; review ${LOG_PATH}"
    exit "${RC_BIN}"
fi

echo "CONTACT_SHEET: PASS"
exit 0
