#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_crossfade_stroke_glyph_id.sh — TICKET-068 hard-gate regression lock
# ═══════════════════════════════════════════════════════════════════════════
#
# Codifies the static-grep regression discipline for Bug #5 / Fase 1#5.
# Bug #5 fix landed at
#   src/backends/software/processors/text_run/text_run_processor.cpp
#   lines 492-496 (stroke branch reads source_placed.glyphs[gi].glyph_id
#   instead of layout.placed.glyphs[gi].glyph_id -- the latter OOBs when
#   outgoing crossfade text has more glyphs than the active text).
#
# Contract (matches tools/check_main_clean.sh):
#   GATE_PASS  -> stdout "GATE_PASS", exit 0  (buggy pattern absent)
#   GATE_FAIL  -> stdout "GATE_FAIL", exit 1, diagnostic on stderr.
#
# This script is the canonical regression-LOCK for the Bug #5 fix.
# The accompanying doctest test
#   tests/text/test_crossfade_stroke.cpp
# establishes the OOB-precondition data shape; this gate ensures the
# source code never trivially regresses back to the buggy literal.
#
# Future fold-in target: tools/test_architectural.sh Section X
# (TICKET-047 slot) — once folded in, this standalone script can be
# retired.  Until then it stands alone so the discipline is enforced
# without waiting on test_architectural.sh reorganisation.
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

TARGET_DIR="src/backends/software/processors/text_run"
# Bug #5 buggy literal.  Fixed-string match (post-escape) so the
# regex meta-characters in `.` `[` `]` don't have to be re-escaped.
BUG_PATTERN='layout.placed.glyphs[gi].glyph_id'

if ! grep -rn -F -- "${BUG_PATTERN}" "${TARGET_DIR}" 2>/dev/null; then
    echo "GATE_PASS"
    exit 0
else
    echo "GATE_FAIL: ${BUG_PATTERN} re-introduced in ${TARGET_DIR}" >&2
    echo "  This is the Bug #5 buggy token -- regression of the" >&2
    echo "  draw_run_layer() stroke branch fix (TICKET-068)." >&2
    echo "  Replace each occurrence with source_placed.glyphs[gi].glyph_id" >&2
    echo "  (active layout, bounded -- safe; layout.placed = outgoing is" >&2
    echo "  unbounded and would OOB when outgoing text has more glyphs)." >&2
    echo "GATE_FAIL"
    exit 1
fi
