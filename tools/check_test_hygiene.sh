#!/usr/bin/env bash
set -euo pipefail

# ── check_test_hygiene.sh ──────────────────────────────────────────────────
#
# Gate: test hygiene invariants (P2-E extended).
#
# Checks:
#   1. DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN must live only in tests/test_main.cpp.
#   2. DOCTEST_SKIP / SKIP / doctest::skip must reference a ticket (TICKET-*)
#      either on the same call site line or in the ±3-line context.
#      The detector is comment-aware (// line + /* */ block) and helper-aware
#      (tests/helpers/ is excluded — those are utilities, not tests). This
#      prevents false positives on SKIP macros documented inside docblocks.
#   3. Every .cpp file with TEST_CASE must have at least one CHECK/REQUIRE.
#      (empty test cases that pass vacuously are a false-positive risk.)
#
# Exit codes:
#   0 = clean
#   1 = violation found
#   2 = internal-script-error (e.g. Python missing or wrong version)
# ────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

fail=0

# ── Check 1: duplicate doctest main ────────────────────────────────────────
bad="$(grep -R "DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\|DOCTEST_CONFIG_IMPLEMENT" \
    "$REPO_ROOT/tests" \
    --include='*.cpp' --include='*.hpp' \
    | grep -v "tests/test_main.cpp" \
    | grep -v "tests/sdk/" || true)"

if [ -n "$bad" ]; then
    echo "HYGIENE_FAIL [1/3]: DOCTEST main must live only in tests/test_main.cpp"
    echo "$bad"
    echo "Fix: remove #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN from the files above."
    fail=1
else
    echo "HYGIENE_PASS [1/3]: no duplicate doctest main definitions found"
fi

# ── Check 2: real skip calls must reference a ticket ──────────────────────
# Replace the previous naive bash-grep (which flagged any line containing
# `DOCTEST_SKIP(` without `TICKET-`, including comment lines) with a
# comment-aware Python parser that preserves line indices and looks at the
# ±3-line context. Rationale documented inline in the heredoc.
#
# Detector recognised: DOCTEST_SKIP(msg), SKIP(msg) (alias macro), doctest::skip(...).
# Helper directory (`tests/helpers/`) is excluded so test utilities don't
# get flagged.
skips_without_ticket="$(python3 - "$REPO_ROOT" <<'PY'
import sys

# Python 3.9+ required for PEP 585 generic annotations (`list[str]`).
if sys.version_info < (3, 9):
    sys.stderr.write(
        "check_test_hygiene: Python 3.9+ required (PEP 585 generic annotations).\n"
        f"  current interpreter: {sys.version}\n"
        f"  interpreter: {sys.executable}\n"
    )
    sys.exit(2)

from pathlib import Path
import re

tests_root = Path(sys.argv[1]) / "tests"

skip_pattern = re.compile(
    r"\b(?:DOCTEST_SKIP|SKIP|doctest::skip)\s*\("
)
ticket_pattern = re.compile(
    r"\bTICKET-[A-Za-z0-9_-]+\b"
)


def strip_comments_preserving_lines(text: str) -> list[str]:
    """Strip C++ comments line-by-line while preserving line indices.

    Returns a list of the same length as `text.splitlines()` where each
    entry is the input line with the comment portion removed (or empty if
    the line was entirely a comment).  Indices line up 1:1 with
    `original_lines` so the context window in caller is correct.
    """
    output = []
    in_block_comment = False

    for raw_line in text.splitlines():
        result = []
        index = 0

        while index < len(raw_line):
            if in_block_comment:
                end = raw_line.find("*/", index)
                if end == -1:
                    index = len(raw_line)
                    continue
                in_block_comment = False
                index = end + 2
                continue

            if raw_line.startswith("//", index):
                break

            if raw_line.startswith("/*", index):
                in_block_comment = True
                index += 2
                continue

            result.append(raw_line[index])
            index += 1

        output.append("".join(result))

    return output


violations = []

for path in sorted(tests_root.rglob("*")):
    if path.suffix not in {".cpp", ".hpp", ".h"}:
        continue

    # Helper utilities (e.g. tests/helpers/doctest_skip_compat.hpp) are not
    # tests and may legitimately re-export or alias SKIP macros.  Exclude.
    if "helpers" in path.parts:
        continue

    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue

    original_lines = text.splitlines()
    code_lines = strip_comments_preserving_lines(text)

    for index, code_line in enumerate(code_lines):
        if not skip_pattern.search(code_line):
            continue

        # ±3-line context window per AGENTS.md "centre-line + nearby
        # context" heuristic.
        context_start = max(0, index - 3)
        context_end = min(len(original_lines), index + 4)
        context = "\n".join(original_lines[context_start:context_end])

        if not ticket_pattern.search(context):
            relative = path.relative_to(tests_root.parent)
            source = original_lines[index].strip()
            violations.append(
                f"{relative}:{index + 1}: {source}"
            )

print("\n".join(violations))
PY
)" || {
    echo "HYGIENE_INTERNAL_ERROR [2/3]: python3 parser failed (see stderr above)"
    fail=1
}

if [ -n "$skips_without_ticket" ]; then
    echo "HYGIENE_FAIL [2/3]: real test skip without nearby TICKET reference:"
    echo "$skips_without_ticket"
    echo "Fix: associate every real skip with an existing tracked ticket."
    fail=1
else
    echo "HYGIENE_PASS [2/3]: all real test skips have a tracked ticket"
fi

# ── Check 3: empty test cases (TEST_CASE without CHECK/REQUIRE) ────────────
# Every .cpp file that defines at least one TEST_CASE must also contain at
# least one CHECK or REQUIRE assertion.  A test case that runs zero assertions
# passes vacuously — which is a false-positive risk.
empty_suites=""
while IFS= read -r f; do
    has_test_case="$(grep -c 'TEST_CASE\|TEST_SUITE_FIXTURE' "$f" || true)"
    if [ "$has_test_case" -gt 0 ]; then
        has_assert="$(grep -cE '\bCHECK\(|\bCHECK_EQ\(|\bCHECK_FALSE\(|\bCHECK_MESSAGE\(|\bCHECK_NOTHROW\(|\bREQUIRE\(|\bREQUIRE_EQ\(|\bREQUIRE_FALSE\(|\bREQUIRE_THROWS\(|\bCHECK_THROWS\(|\bFAIL\(|\bemit_preset_gate\(' "$f" || true)"
        if [ "$has_assert" -eq 0 ]; then
            empty_suites="${empty_suites}
  $f"
        fi
    fi
done < <(find "$REPO_ROOT/tests" -name '*.cpp' -not -path '*/sdk/*' -not -name 'test_main.cpp')

if [ -n "$empty_suites" ]; then
    echo "HYGIENE_FAIL [3/3]: TEST_CASE files with zero assertions (vacuous pass):"
    echo "$empty_suites"
    echo "Fix: add at least one CHECK/REQUIRE to each test file."
    fail=1
else
    echo "HYGIENE_PASS [3/3]: all TEST_CASE files have at least one assertion"
fi

# ── Final verdict ──────────────────────────────────────────────────────────
if [ "$fail" -ne 0 ]; then
    echo ""
    echo "HYGIENE_FAIL: $fail check(s) failed."
    exit 1
fi

echo ""
echo "HYGIENE_PASS: all 3 test hygiene checks passed."
exit 0
