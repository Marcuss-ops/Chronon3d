#!/usr/bin/env bash
set -euo pipefail

# ── check_test_suite_registration.sh ────────────────────────────────────────
#
# Audit gate: lists all test cmake files that use raw add_executable
# instead of chronon3d_add_test_suite().
#
# This is an INFORMATIONAL gate (exit 0 always) until the migration
# is complete. Once all targets are converted, change to exit 1 to
# enforce the convention.
#
# Exit codes:
#   0 = audit complete (always, informational only)
# ────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Test Suite Registration Audit ==="
echo ""

raw_count=0
suite_count=0

for f in "$REPO_ROOT"/tests/*.cmake; do
    [ -f "$f" ] || continue
    basename=$(basename "$f")

    raw=$(grep 'add_executable(chronon3d_.*test' "$f" 2>/dev/null | grep -v '^#' | wc -l || echo 0)
    suite=$(grep 'chronon3d_add_test_suite(' "$f" 2>/dev/null | grep -v '^#' | wc -l || echo 0)

    if [ "$raw" -gt 0 ]; then
        echo "  RAW  $basename  ($raw targets)"
        raw_count=$((raw_count + raw))
    fi
    if [ "$suite" -gt 0 ]; then
        echo "  SUITE $basename ($suite targets)"
        suite_count=$((suite_count + suite))
    fi
done

echo ""
echo "Summary: $raw_count targets use raw add_executable, $suite_count use chronon3d_add_test_suite"
echo ""
echo "NOTE: This is an informational audit. Convert targets incrementally,"
echo "one cmake file per commit, verifying build after each."
exit 0
