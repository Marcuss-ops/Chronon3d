#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

FIXTURE="$TMP_ROOT/fixture"
mkdir -p "$FIXTURE/tools" "$FIXTURE/tests" "$FIXTURE/docs/tickets"
cp "$SCRIPT_DIR/check_test_hygiene.sh" "$FIXTURE/tools/check_test_hygiene.sh"
: > "$FIXTURE/tests/test_main.cpp"
cat > "$FIXTURE/tests/fixture.cpp" <<'CPP'
TEST_CASE("Live skipped test" * doctest::skip(
    "TICKET-DOCTEST-SKIP-ROT SKIP-001: fixture reason (with punctuation)")) {
    CHECK(true);
}
CPP

write_registry() {
    local row="$1"
    cat > "$FIXTURE/docs/tickets/TICKET-DOCTEST-SKIP-ROT.md" <<EOF
## Registry

| Row | Test | Reason | Owner | Deadline | Reactivation condition |
|---|---|---|---|---|---|
$row

## Rules
EOF
}

write_empty_registry() {
    cat > "$FIXTURE/docs/tickets/TICKET-DOCTEST-SKIP-ROT.md" <<'EOF'
## Registry

| Row | Test | Reason | Owner | Deadline | Reactivation condition |
|---|---|---|---|---|---|

## Rules
EOF
}

future_deadline="$(python3 -c 'from datetime import date, timedelta; print((date.today() + timedelta(days=30)).isoformat())')"
valid_row="| SKIP-001 | \`tests/fixture.cpp\` :: \"Live skipped test\" | video host | owner | ${future_deadline} | host available |"
write_registry "$valid_row"
if ! bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/valid.log" 2>&1; then
    cat "$TMP_ROOT/valid.log"
    echo "HYGIENE_SELFTEST_FAIL: valid registry fixture was rejected"
    exit 1
fi

write_empty_registry
if bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/unregistered.log" 2>&1; then
    echo "HYGIENE_SELFTEST_FAIL: skip without registry row was accepted"
    exit 1
fi
if ! grep -q "SKIP-001 has no registry row" "$TMP_ROOT/unregistered.log"; then
    cat "$TMP_ROOT/unregistered.log"
    echo "HYGIENE_SELFTEST_FAIL: unregistered skip was not diagnosed"
    exit 1
fi

write_registry "$valid_row"
cat >"$FIXTURE/tests/fixture.cpp" <<'CPP'
TEST_CASE("Live skipped test") {
    CHECK(true);
}
CPP
if bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/stale.log" 2>&1; then
    echo "HYGIENE_SELFTEST_FAIL: stale registry row was accepted"
    exit 1
fi
if ! grep -q "SKIP-001: registry row has no live doctest::skip" "$TMP_ROOT/stale.log"; then
    cat "$TMP_ROOT/stale.log"
    echo "HYGIENE_SELFTEST_FAIL: stale registry row was not diagnosed"
    exit 1
fi

cat >"$FIXTURE/tests/fixture.cpp" <<'CPP'
TEST_CASE("Live skipped test" * doctest::skip(
    "TICKET-DOCTEST-SKIP-ROT SKIP-001: fixture reason (with punctuation)")) {
    CHECK(true);
}
CPP
write_registry '| SKIP-001 | `tests/fixture.cpp` :: "Unrelated test" | video host | owner | 2026-09-30 | host available |'
if bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/wrong-test.log" 2>&1; then
    echo "HYGIENE_SELFTEST_FAIL: mismatched registry TEST_CASE was accepted"
    exit 1
fi
if ! grep -q "skip is under 'Live skipped test', expected 'Unrelated test'" "$TMP_ROOT/wrong-test.log"; then
    cat "$TMP_ROOT/wrong-test.log"
    echo "HYGIENE_SELFTEST_FAIL: TEST_CASE mismatch was not diagnosed"
    exit 1
fi

write_registry '| SKIP-001 | `tests/fixture.cpp` :: "Live skipped test" | video host | owner |'
if bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/malformed.log" 2>&1; then
    echo "HYGIENE_SELFTEST_FAIL: malformed registry row was accepted"
    exit 1
fi
if ! grep -q "malformed SKIP row" "$TMP_ROOT/malformed.log"; then
    cat "$TMP_ROOT/malformed.log"
    echo "HYGIENE_SELFTEST_FAIL: malformed row was not diagnosed"
    exit 1
fi

write_registry "$valid_row
$valid_row"
if bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/duplicate.log" 2>&1; then
    echo "HYGIENE_SELFTEST_FAIL: duplicate registry id was accepted"
    exit 1
fi
if ! grep -q "duplicate SKIP ids in registry" "$TMP_ROOT/duplicate.log"; then
    cat "$TMP_ROOT/duplicate.log"
    echo "HYGIENE_SELFTEST_FAIL: duplicate id was not diagnosed"
    exit 1
fi

write_registry '| SKIP-001 | `tests/fixture.cpp` :: "Live skipped test" | video host | owner | 2000-01-01 | host available |'
if bash "$FIXTURE/tools/check_test_hygiene.sh" >"$TMP_ROOT/expired.log" 2>&1; then
    echo "HYGIENE_SELFTEST_FAIL: expired registry row was accepted"
    exit 1
fi
if ! grep -q "registry deadline 2000-01-01 has passed" "$TMP_ROOT/expired.log"; then
    cat "$TMP_ROOT/expired.log"
    echo "HYGIENE_SELFTEST_FAIL: expired deadline was not diagnosed"
    exit 1
fi

echo "HYGIENE_SELFTEST_PASS: valid, unregistered, stale, mismatched, malformed, duplicate, and expired registry fixtures verified"
