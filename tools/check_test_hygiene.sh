#!/usr/bin/env bash
set -euo pipefail

# Cat-3 IO exclusion wrapper (TICKET-PRE-PUSH-IO-EXCLUSIONS).
grep() { command grep --exclude-dir=vcpkg_installed --exclude-dir=build --exclude-dir=.cache --exclude-dir=node_modules "$@"; }

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
#   3. Every doctest::skip maps to one live registry row, source file, and
#      enclosing TEST_CASE; malformed rows and duplicate ids fail.
#   4. Every .cpp file with TEST_CASE must have at least one CHECK/REQUIRE.
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
    echo "HYGIENE_FAIL [1/4]: DOCTEST main must live only in tests/test_main.cpp"
    echo "$bad"
    echo "Fix: remove #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN from the files above."
    fail=1
else
    echo "HYGIENE_PASS [1/4]: no duplicate doctest main definitions found"
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
    echo "HYGIENE_INTERNAL_ERROR [2/4]: python3 parser failed (see stderr above)"
    fail=1
}

if [ -n "$skips_without_ticket" ]; then
    echo "HYGIENE_FAIL [2/4]: real test skip without nearby TICKET reference:"
    echo "$skips_without_ticket"
    echo "Fix: associate every real skip with an existing tracked ticket."
    fail=1
else
    echo "HYGIENE_PASS [2/4]: all real test skips have a tracked ticket"
fi

# ── Check 3: doctest skip registry is bidirectionally complete ────────────
# Ticket references alone are not enough: each executable doctest::skip
# must name a registry row, every row must still point at a live skip, and
# the registry must not contain duplicate ids.
if registry_result="$(python3 - "$REPO_ROOT" <<'PY'
from pathlib import Path
from datetime import date
import re
import sys

root = Path(sys.argv[1])
registry = root / "docs/tickets/TICKET-DOCTEST-SKIP-ROT.md"
registry_text = registry.read_text(encoding="utf-8")
errors = []
registry_section = registry_text.split("## Registry", 1)[-1].split("## Rules", 1)[0]
rows = []
table_started = False
for line_number, line in enumerate(registry_section.splitlines(), 1):
    stripped = line.strip()
    if stripped.startswith("| Row |"):
        table_started = True
        continue
    if not table_started or not stripped.startswith("|"):
        continue
    cells = [cell.strip() for cell in stripped.strip("|").split("|")]
    if cells and all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells):
        continue
    if len(cells) != 6 or not re.fullmatch(r"SKIP-\d{3}", cells[0]):
        errors.append(f"registry line {line_number}: malformed SKIP row")
        continue
    location = re.fullmatch(r"`([^`]+)`\s*::\s*\"([^\"]+)\"", cells[1])
    if not location or any(not cell for cell in cells[2:]):
        errors.append(f"{cells[0]}: missing test, reason, owner, deadline, or reactivation condition")
        continue
    try:
        deadline = date.fromisoformat(cells[4])
    except ValueError:
        errors.append(f"{cells[0]}: deadline must use YYYY-MM-DD")
        continue
    if deadline < date.today():
        errors.append(f"{cells[0]}: registry deadline {deadline.isoformat()} has passed")
        continue
    rows.append((cells[0], location.group(1), location.group(2), line_number))

row_ids = [row_id for row_id, _, _, _ in rows]
if len(row_ids) != len(set(row_ids)):
    errors.append("duplicate SKIP ids in registry")
registered = set(row_ids)
seen = set()
row_specs = {row_id: (path, test_name) for row_id, path, test_name, _ in rows}
skip_call = re.compile(r"\bdoctest::skip\s*\(")
row_ref = re.compile(r"\bSKIP-\d{3}\b")
test_case = re.compile(r"\bTEST_CASE\s*\(")


def mask_cpp_comments_and_literals(text):
    """Blank comments and literals while preserving offsets and newlines."""
    chars = list(text)
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
        elif text.startswith("/*", index):
            close = text.find("*/", index + 2)
            end = len(text) if close < 0 else close + 2
        elif text.startswith('R"', index):
            open_paren = text.find("(", index + 2, min(len(text), index + 20))
            if open_paren < 0:
                index += 1
                continue
            delimiter = text[index + 2:open_paren]
            close = text.find(")" + delimiter + '"', open_paren + 1)
            end = len(text) if close < 0 else close + len(delimiter) + 2
        elif text[index] in {'"', "'"}:
            quote = text[index]
            end = index + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == quote:
                    end += 1
                    break
                end += 1
        else:
            index += 1
            continue

        for cursor in range(index, min(end, len(text))):
            if chars[cursor] != "\n":
                chars[cursor] = " "
        index = end
    return "".join(chars)


def test_declarations(text, code):
    """Return (source offset, title) pairs for real TEST_CASE declarations."""
    declarations = []
    for match in test_case.finditer(code):
        literal = re.match(r'\s*"((?:\\.|[^"\\])*)"', text[match.end():])
        if literal:
            declarations.append((match.start(), literal.group(1)))
    return declarations

for path in sorted((root / "tests").rglob("*")):
    if path.suffix not in {".cpp", ".hpp", ".h"} or "helpers" in path.parts:
        continue
    text = path.read_text(encoding="utf-8")
    # Keep offsets stable while masking comments/literals. This ignores fake
    # calls in comments/strings and makes parenthesis matching insensitive to
    # punctuation in skip-reason text.
    code = mask_cpp_comments_and_literals(text)
    declarations = test_declarations(text, code)
    for match in skip_call.finditer(code):
        depth = 1
        end = match.end()
        while end < len(code) and depth:
            if code[end] == "(": depth += 1
            elif code[end] == ")": depth -= 1
            end += 1
        call = code[match.start():end]
        ids = row_ref.findall(text[match.start():end])
        rel = path.relative_to(root)
        if len(ids) != 1:
            errors.append(f"{rel}: doctest::skip must cite exactly one SKIP-NNN row")
            continue
        row_id = ids[0]
        seen.add(row_id)
        if row_id not in registered:
            errors.append(f"{rel}: {row_id} has no registry row")
        else:
            expected_path, expected_test = row_specs[row_id]
            if str(rel) != expected_path:
                errors.append(f"{rel}: {row_id} registry path is {expected_path}")
            if not any(title == expected_test for _, title in declarations):
                errors.append(f"{row_id}: registered TEST_CASE {expected_test!r} not found in {expected_path}")
            enclosing = [title for position, title in declarations if position <= match.start()]
            if not enclosing or enclosing[-1] != expected_test:
                actual = enclosing[-1] if enclosing else "<no enclosing TEST_CASE>"
                errors.append(f"{rel}: {row_id} skip is under {actual!r}, expected {expected_test!r}")

for row_id in sorted(registered - seen):
    errors.append(f"{row_id}: registry row has no live doctest::skip")

if errors:
    print("\n".join(errors))
    sys.exit(1)
print(f"{len(registered)} registry rows match live doctest::skip calls")
PY
)"; then
    echo "HYGIENE_PASS [3/4]: $registry_result"
else
    echo "HYGIENE_FAIL [3/4]: doctest skip registry does not match live calls"
    echo "$registry_result"
    fail=1
fi

# ── Check 4: empty test cases (TEST_CASE without CHECK/REQUIRE) ────────────
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
    echo "HYGIENE_FAIL [4/4]: TEST_CASE files with zero assertions (vacuous pass):"
    echo "$empty_suites"
    echo "Fix: add at least one CHECK/REQUIRE to each test file."
    fail=1
else
    echo "HYGIENE_PASS [4/4]: all TEST_CASE files have at least one assertion"
fi

# ── Final verdict ──────────────────────────────────────────────────────────
if [ "$fail" -ne 0 ]; then
    echo ""
    echo "HYGIENE_FAIL: $fail check(s) failed."
    exit 1
fi

echo ""
echo "HYGIENE_PASS: all 4 test hygiene checks passed."
exit 0
