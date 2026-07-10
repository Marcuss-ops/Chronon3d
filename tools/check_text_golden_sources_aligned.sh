#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/check_text_golden_sources_aligned.sh
#
# Forward-only CI gate for TICKET-TEXT-GOLDEN-SOURCES-ALIGNED.
#
# Detects "add_test without target_sources" misalignment in
# tests/text_golden_tests.cmake — every `add_test(NAME TextMultilingual*)`
# entry MUST have a matching `target_sources(... text_multilingual/NN_*.cpp)`
# entry, otherwise the build silently skips the test file (the .cpp file is
# in the directory but not in the cmake target's source list).
#
# This bug class bit the project twice:
#   - cycle 4 (commit 5efcc301, TICKET-FASE3-MULTILINGUAL §HangulComposition):
#     the 04_hangul_composition.cpp file was added to the directory but the
#     target_sources registration was forgotten — the build would have
#     skipped 04 entirely. Caught + fixed in cycle 5 (commit 21e15e91).
#   - cycle 4 also left the TextMultilingualMixedBaseline add_test block
#     syntactically broken (the COMMAND/WORKING_DIRECTORY/`) lines were
#     dangling after the TextMultilingualHangulComposition block).
#
# This gate hard-blocks any `git push` that introduces a TextMultilingual
# add_test without a matching target_sources entry.
#
# Exit codes:
#   0 = clean (all TextMultilingual add_test entries have matching target_sources)
#   1 = GATE_FAIL (one or more mismatches; remediation hint on stderr)
#   2 = INTERNAL_ERROR (file not accessible, etc.)
#
# Usage:
#   bash tools/check_text_golden_sources_aligned.sh
#
# Invoked by:
#   - tools/wrap_push.sh Step 4.5e (canonical, repo-tracked, all atomic commits)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

CMAKELIST="${REPO_ROOT}/tests/text_golden_tests.cmake"

if [ ! -f "$CMAKELIST" ]; then
    # If the file doesn't exist, there's nothing to check.
    # Not a GATE_FAIL — just skip silently.
    echo "OK: $CMAKELIST not present (skipping check)"
    exit 0
fi

# ── Step 1: extract all add_test names that start with TextMultilingual ───
# The .cmake uses multi-line add_test blocks (NAME on a separate line from
# add_test()), so we grep for `NAME TextMultilingual` directly (which is on
# a single line) rather than for the full add_test(...NAME...) pattern.
# Pattern: NAME\s+TextMultilingual{CAMEL_NAME}
#   - `NAME` — literal keyword (always followed by whitespace + the name)
#   - `\s+` — required whitespace between NAME and the test name
#   - `TextMultilingual` — literal prefix
#   - `[A-Za-z0-9_]+` — the test family name (CamelCase, e.g., "KerningPairs")
# Defensive: filter out commented-out lines (`# NAME TextMultilingualFoo`)
# before extracting names — without this filter, a commented-out example
# in the .cmake would cause a spurious GATE_FAIL for a non-existent test.
ADD_TEST_NAMES=$(grep -v '^[[:space:]]*#' "$CMAKELIST" \
    | grep -oE 'NAME[[:space:]]+TextMultilingual[A-Za-z0-9_]+' \
    | sed -E 's/^NAME[[:space:]]+TextMultilingual//' \
    | sort -u)

# ── Step 2: extract all target_sources files under text_multilingual/ ─────
# Pattern: text_multilingual/{NN}_{snake_name}.cpp
#   - `text_multilingual/` — literal directory path prefix
#   - `[0-9]+_` — NN prefix (1+ digits + underscore)
#   - `[a-z0-9_]+\.cpp` — snake_case filename
# This pattern handles multi-line target_sources blocks (the file path is
# on the inner line, not the same line as the `target_sources(` call).
SOURCE_FILES=$(grep -oE 'text_multilingual/[0-9]+_[a-z0-9_]+\.cpp' "$CMAKELIST" \
    | sed 's|^text_multilingual/||' \
    | sort -u)

if [ -z "$ADD_TEST_NAMES" ]; then
    # No TextMultilingual add_test entries to check — skip silently.
    # This is the "no multilingual tests yet" state (e.g., a fresh
    # checkout before TICKET-FASE3-MULTILINGUAL landed).
    echo "OK: no TextMultilingual add_test entries in $CMAKELIST (skipping check)"
    exit 0
fi

# ── Step 3: helper — convert CamelCase to snake_case ─────────────────────
# Algorithm: insert _ before each uppercase letter that follows a lowercase
# letter or digit, then lowercase everything.
#   "KerningPairs"           → "kerning_pairs"
#   "MixedAdvanceWidths"     → "mixed_advance_widths"
#   "HangulComposition"      → "hangul_composition"
#   "DevanagariConjuncts"    → "devanagari_conjuncts"
#   "ArabicShaping"          → "arabic_shaping"
#   "HebrewNikud"            → "hebrew_nikud"
camel_to_snake() {
    echo "$1" | sed -E 's/([a-z0-9])([A-Z])/\1_\2/g' | tr '[:upper:]' '[:lower:]'
}

# ── Step 4: check each add_test name has a matching file ─────────────────
EXIT_CODE=0
MISSING=()
TOTAL=$(echo "$ADD_TEST_NAMES" | wc -l)
CHECKED=0

for name in $ADD_TEST_NAMES; do
    snake_name=$(camel_to_snake "$name")
    # Look for a file matching the snake_case name (any NN_ prefix).
    # Anchored regex `^[0-9]+_${snake_name}\.cpp$` to avoid false-positives
    # (e.g., "kerning_pairs_extra.cpp" would NOT match).
    MATCHING_FILE=$(echo "$SOURCE_FILES" | grep -E "^[0-9]+_${snake_name}\.cpp$" | head -1 || true)
    if [ -n "$MATCHING_FILE" ]; then
        CHECKED=$((CHECKED + 1))
        echo "  OK: TextMultilingual${name} ↔ ${MATCHING_FILE}"
    else
        MISSING+=("TextMultilingual${name} (no target_sources entry for NN_${snake_name}.cpp under text_multilingual/)")
        EXIT_CODE=1
    fi
done

# ── Step 5: emit gate result ─────────────────────────────────────────────
if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo "OK: all $CHECKED TextMultilingual add_test entries have matching target_sources entries"
    exit 0
else
    echo "" >&2
    echo "GATE_FAIL: $TOTAL TextMultilingual add_test entries checked, ${#MISSING[@]} missing target_sources" >&2
    for m in "${MISSING[@]}"; do
        echo "  - $m" >&2
    done
    echo "" >&2
    echo "Remediation: for each missing entry above, add a target_sources line" >&2
    echo "in tests/text_golden_tests.cmake:" >&2
    echo "" >&2
    echo "  target_sources(chronon3d_text_golden_tests PRIVATE" >&2
    echo "      text_golden/text_multilingual/NN_<name>.cpp)" >&2
    echo "" >&2
    echo "where NN_ is the next available 2-digit prefix and <name>.cpp is the" >&2
    echo "snake_case of the add_test name suffix (e.g., TextMultilingualArabicShaping" >&2
    echo "→ 06_arabic_shaping.cpp). The snake_case conversion is automatic" >&2
    echo "(CamelCase → _ before uppercase + lowercase)." >&2
    echo "" >&2
    echo "verify: bash $0" >&2
    echo "        should return OK with all entries listed." >&2
    exit 1
fi
