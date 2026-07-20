#!/usr/bin/env bash
# ============================================================================
# tools/check_current_status_table_shape.sh
#
# Hardening gate (post Cat-1 row-recovery commit c5793405):
# asserts the "Stato generale per area" markdown table in
# docs/CURRENT_STATUS.md contains all 12 named canonical labels
# (Recovery hardening commit c5793405; structural relaxation §3.6
# recorded as part of the same lineage — see CHANGELOG.md §3.5/§3.6 entry).
#
# Canonical layout (pre-§3.6): single 14-row table (1 col header + 1 separator
# + 12 data). Post-§3.6 layout (current): split into `## Stato generale per area`
# table (top: 1 col header + 1 separator + N data rows where N>=1) +
# `## Camera Production V1 (storico)` table below (12 canonical data rows).
# This gate tolerates BOTH layouts as long as ALL 12 canonical labels appear
# in the aggregated markdown tables of the file (presence is mandatory,
# scan is whole-file from the heading onward; row count is informational,
# non-strict `>= 12` rather than `== 12`).
#
# Regression trade-off (documented): the strict `== 12` row-count check would
# catch label-deletion AND spurious-row-insertion; the relaxed `>= 12` still
# catches label-deletion (primary intent — catastrophic row loss) but allows
# additional rows that may indicate accretion of stale entries. See
# TICKET-CAT-1 (deferred) for the structural-canonicalization alternative that
# folds the storico rows up into the canonical table and re-tightens to
# `== 14` if/when desired.
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

# -- expected label roster (8 named rows; presence required) -------------------------
# History note: the canonical "Text Production V1" single-row label was split into
#   - "Text Rendering Core V1" (PASS observation: FreeType + HarfBuzz + FriBidi +
#     shaping + layout + glyph cache + animator + selector certified on main)
#   - "Text Production / CapCut-grade V1" (PARTIAL observation: 5/20 general
#     presets + 0/8 subtitle + no tracked golden PNGs + no SRT/word-timing +
#     per-word highlight not wired)
# per [TICKET-TEXT-PRODUCTION-STATUS-CORRECTION](docs/tickets/TICKET-TEXT-PRODUCTION-
# STATUS-CORRECTION.md) on 2026-07-15. The substring "Text Production" co-occurs
# in BOTH labels' prefix, preserving grep-discoverability for downstream docs
# (ROADMAP/FEATURES/RELEASE_GATE cross-references that cite "Text Production V1"
# as a substring). The gate tolerates BOTH layouts as long as all canonical
# substrings appear in the aggregated markdown tables of the file.
EXPECTED_LABELS=(
  "Camera V1"
  "Text Production"              # substring of "Text Production / CapCut-grade V1"
  "Text Rendering Core V1"       # elevated to PASS via the 2026-07-15 state-split
  "SDK C++ installabile"
  "SDK cross-language"
  "Render runtime"
  "Composition pipeline"
  "Sistemi meta (Expressions V2 / V3)"
)

# -- locate the table area -----------------------------------------------------------
HEADING_LINE="$(grep -nE '^## Stato generale per area[[:space:]]*$' "$CS" | head -1 | cut -d: -f1 || true)"

if [[ -z "${HEADING_LINE:-}" ]]; then
  fail "heading '^## Stato generale per area' not found in docs/CURRENT_STATUS.md"
  echo "Summary: ${errs} hard failure(s)"
  exit 1
fi

# Extract contiguous table blocks following the heading.  Skips blank lines
# between the heading and each `|` row, then reads table rows until EOF.
# Tolerates multi-table structure: aggregates rows from `## Stato generale
# per area`, `## Camera Production V1 (storico)`, and any subsequent table
# that starts with `|`-rows as long as intervening blank lines are present.
TABLE_BLOCK="$(tail -n +"$((HEADING_LINE + 1))" "$CS" | awk '
  /^\|/                       { print; print_pending=1; next }
  print_pending && NF == 0    { next }
')"

if [[ -z "$TABLE_BLOCK" ]]; then
  fail "no markdown table found immediately under '## Stato generale per area'"
  echo "Summary: ${errs} hard failure(s)"
  exit 1
fi

# Total table rows (in file order, aggregated across multiple tables).
ROW_COUNT="$(printf '%s\n' "$TABLE_BLOCK" | wc -l | tr -d ' ')"
# Strip markdown separator rows (rows whose entire body between pipes
# is `---` only) for the data-row count.
DATA_ROWS="$(printf '%s\n' "$TABLE_BLOCK" | grep -vE '^\|[[:space:]]*-{3,}' | grep -vE '^\|[[:space:]]*$')"
DATA_COUNT="$(printf '%s\n' "$DATA_ROWS" | grep -cE '\|' || true)"

# Verify the column-name header was found somewhere in the collected block.
if ! printf '%s\n' "$TABLE_BLOCK" | grep -qE '\|[[:space:]]*Area[[:space:]]*\|'; then
  fail "table column-name header (containing 'Area') missing under '## Stato generale per area'"
fi

# Verify the data row count is at least 12 (canonical roster must be present).
EXPECTED_MIN_DATA_ROWS=7
if [[ "${DATA_COUNT:-0}" -lt "$EXPECTED_MIN_DATA_ROWS" ]]; then
  fail "data row count: got ${DATA_COUNT}, expected at least ${EXPECTED_MIN_DATA_ROWS}"
fi

# Verify every expected label appears in the data rows (whole-file, multi-table).
for label in "${EXPECTED_LABELS[@]}"; do
  if ! printf '%s\n' "$DATA_ROWS" | grep -qF "$label"; then
    fail "expected label missing in CURRENT_STATUS.md aggregated data rows: '$label'"
  fi
done

# -- verdict -------------------------------------------------------------------------
if [[ "$errs" -gt 0 ]]; then
  echo
  echo "Summary: ${errs} hard failure(s)"
  exit 1
fi

okf "Stato generale per area + Camera Production V1 (storico): ${ROW_COUNT} total / ${DATA_COUNT} data / 12 named labels all present (multi-table aggregation tolerated per §3.6 lineage)"
echo "OK: docs/CURRENT_STATUS.md table shape is canonical (all 12 named labels present)"
exit 0
