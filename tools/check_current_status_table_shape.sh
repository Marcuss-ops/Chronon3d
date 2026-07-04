#!/usr/bin/env bash
# ============================================================================
# tools/check_current_status_table_shape.sh
#
# Hardening gate (post Cat-1 row-recovery commit c5793405):
# asserts the "Stato generale per area" markdown table in
# docs/CURRENT_STATUS.md is in canonical shape.
#
# Canonical shape:
#   - 1 column-name header row  : `| Area | Stato | ... |`
#   - 1 separator row           : `|---|...|`
#   - 12 named data rows
#   Total: 14 table rows, of which 13 are body rows (separator + 12 data).
#
# The 12 named labels are exactly (presence is mandatory, order is not):
#   - Render graph compilato
#   - Software backend
#   - Execution scope (precomp + nested)
#   - Text Production V1
#   - Camera Production V1
#   - SDK C++ installabile
#   - SDK cross-language
#   - Sistemi meta (Expressions V2 / V3 tile-first)
#   - Render runtime (session + caches)
#   - Render engine construction
#   - Composition pipeline
#   - Text pre-compilation (CompiledTextRun)
#
# Why this gate exists:
#   Commit 7ee302bf's recovery commit history shows that a faulty
#   `sed 's/^| Camera Production V1.*//'` over-matched two rows when both
#   contained the substring "Projection contract closed (C1..." (a HEAD-side
#   row + a REMOTE-side row in a rebase conflict context), deleting the
#   Camera Production V1 row from CURRENT_STATUS.md. The recovery was
#   manual (commit c5793405). This gate ensures the next regression of
#   that shape surfaces as a [FAIL] exit code, not as silent row drift.
#
# Usage:
#   tools/check_current_status_table_shape.sh                  # exit-code check
#
# Exit codes:
#   0 — table shape is canonical (PASS)
#   1 — at least one hard failure (FAIL)
#
# Style mirrors tools/check_doc_sync.sh:
#   - set -euo pipefail
#   - [ OK ] / [FAIL] prefixes
#   - trailing summary line
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CS="$ROOT/docs/CURRENT_STATUS.md"

errs=0
fail() { echo "[FAIL] $1" >&2; errs=$((errs + 1)); }
okf()  { echo "[ OK ] $1"; }

# -- preconditions ------------------------------------------------------------------
[[ -f "$CS" ]] || { echo "[FAIL] missing $CS" >&2; exit 1; }

# -- expected label roster (12 named rows; presence required) -------------------------
EXPECTED_LABELS=(
  "Render graph compilato"
  "Software backend"
  "Execution scope (precomp + nested)"
  "Text Production V1"
  "Camera Production V1"
  "SDK C++ installabile"
  "SDK cross-language"
  "Sistemi meta (Expressions V2 / V3 tile-first)"
  "Render runtime (session + caches)"
  "Render engine construction"
  "Composition pipeline"
  "Text pre-compilation (CompiledTextRun)"
)

# -- locate the table area -----------------------------------------------------------
HEADING_LINE="$(grep -nE '^## Stato generale per area[[:space:]]*$' "$CS" | head -1 | cut -d: -f1 || true)"

if [[ -z "${HEADING_LINE:-}" ]]; then
  fail "heading '^## Stato generale per area' not found in docs/CURRENT_STATUS.md"
  echo "Summary: ${errs} hard failure(s)"
  exit 1
fi

# Extract contiguous table block following the heading.  Skips blank lines
# between the heading and the first `|` row, then reads table rows until
# either a blank line or a new '## ' heading is encountered.
TABLE_BLOCK="$(tail -n +"$((HEADING_LINE + 1))" "$CS" | awk '
  /^\|/                       { print; in_table=1; next }
  in_table && (NF == 0 || /^## /) { exit }
')"

if [[ -z "$TABLE_BLOCK" ]]; then
  fail "no markdown table found immediately under '## Stato generale per area'"
  echo "Summary: ${errs} hard failure(s)"
  exit 1
fi

# Total table rows (in file order).
ROW_COUNT="$(printf '%s\n' "$TABLE_BLOCK" | wc -l | tr -d ' ')"
EXPECTED_TOTAL_ROWS=14
EXPECTED_BODY_ROWS=13
EXPECTED_DATA_ROWS=12

if [[ "$ROW_COUNT" -ne "$EXPECTED_TOTAL_ROWS" ]]; then
  fail "table row count drift: got ${ROW_COUNT}, expected ${EXPECTED_TOTAL_ROWS} (1 col header + 1 separator + ${EXPECTED_DATA_ROWS} data)"
fi

# -- column-name header (row 1) ------------------------------------------------------
COLUMN_HEADER="$(printf '%s\n' "$TABLE_BLOCK" | sed -n '1p')"
if ! printf '%s' "$COLUMN_HEADER" | grep -qE '\|[[:space:]]*Area[[:space:]]*\|'; then
  fail "row 1 is not the expected column-name header (missing 'Area'). Got: '$COLUMN_HEADER'"
fi

# -- separator (row 2) --------------------------------------------------------------
SEPARATOR="$(printf '%s\n' "$TABLE_BLOCK" | sed -n '2p')"
if ! printf '%s' "$SEPARATOR" | grep -qE '\|[[:space:]]*-{3,}[[:space:]]*\|'; then
  fail "row 2 is not a markdown separator (expected |---|…|). Got: '$SEPARATOR'"
fi

# -- data rows (rows 3..N) -----------------------------------------------------------
DATA_ROWS="$(printf '%s\n' "$TABLE_BLOCK" | sed -n '3,$p')"
# awk already filtered to `|`-starting lines, so wc -l is equivalent to grep -cE '^\|'
# and avoids the pipefail interaction with grep -c exiting 1 on zero matches.
DATA_COUNT="$(printf '%s\n' "$DATA_ROWS" | wc -l | tr -d ' ')"

if [[ "$DATA_COUNT" -ne "$EXPECTED_DATA_ROWS" ]]; then
  fail "data row count: got ${DATA_COUNT}, expected ${EXPECTED_DATA_ROWS}"
fi

# Verify every expected label appears in the data rows.
for label in "${EXPECTED_LABELS[@]}"; do
  if ! printf '%s\n' "$DATA_ROWS" | grep -qF "$label"; then
    fail "expected label missing in 'Stato generale per area' data rows: '$label'"
  fi
done

# -- verdict -------------------------------------------------------------------------
if [[ "$errs" -gt 0 ]]; then
  echo
  echo "Summary: ${errs} hard failure(s)"
  exit 1
fi

okf "Stato generale per area: ${ROW_COUNT} total / ${EXPECTED_BODY_ROWS} body (1 separator + ${EXPECTED_DATA_ROWS} data) / 12 named labels all present"
echo "OK: docs/CURRENT_STATUS.md table shape is canonical"
exit 0
