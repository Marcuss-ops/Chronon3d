#!/usr/bin/env bash
set -euo pipefail

# ── check_test_hygiene.sh ──────────────────────────────────────────────────
#
# Gate: test hygiene invariants (P2-E extended).
#
# Checks:
#   1. DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN must live only in tests/test_main.cpp.
#   2. DOCTEST_SKIP must reference a ticket (TICKET-*) to track known skips.
#   3. Every .cpp file with TEST_CASE must have at least one CHECK/REQUIRE.
#      (empty test cases that pass vacuously are a false-positive risk.)
#
# Exit codes:
#   0 = clean
#   1 = violation found
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

# ── Check 2: DOCTEST_SKIP must reference a ticket ─────────────────────────
# Every DOCTEST_SKIP("...") must contain "TICKET-" in its message to ensure
# skipped tests are tracked as open work, not silently forgotten.
skips_without_ticket="$(grep -rn 'DOCTEST_SKIP(' "$REPO_ROOT/tests" \
    --include='*.cpp' --include='*.hpp' \
    | grep -v 'TICKET-' || true)"

if [ -n "$skips_without_ticket" ]; then
    echo "HYGIENE_FAIL [2/3]: DOCTEST_SKIP without TICKET- reference:"
    echo "$skips_without_ticket"
    echo "Fix: add a TICKET-NNN reference to every DOCTEST_SKIP message."
    fail=1
else
    echo "HYGIENE_PASS [2/3]: all DOCTEST_SKIP references include a TICKET-"
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
