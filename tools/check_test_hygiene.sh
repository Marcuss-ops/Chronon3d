#!/usr/bin/env bash
set -euo pipefail

# ── check_test_hygiene.sh ──────────────────────────────────────────────────
#
# Gate: DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN must live only in tests/test_main.cpp.
# Any other file defining it will cause duplicate symbol linker errors when
# linked with the shared test_main.cpp.
#
# Exit codes:
#   0 = clean
#   1 = violation found
# ────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

bad="$(grep -R "DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\|DOCTEST_CONFIG_IMPLEMENT" \
    "$REPO_ROOT/tests" \
    --include='*.cpp' --include='*.hpp' \
    | grep -v "tests/test_main.cpp" \
    | grep -v "tests/sdk/" || true)"

if [ -n "$bad" ]; then
    echo "HYGIENE_FAIL: DOCTEST main must live only in tests/test_main.cpp"
    echo ""
    echo "$bad"
    echo ""
    echo "Fix: remove #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN from the files above."
    echo "     They should use #include <doctest/doctest.h> instead."
    exit 1
fi

echo "HYGIENE_PASS: no duplicate doctest main definitions found"
exit 0
