// selftest_check_test_hygiene_python.cpp - contract selftest for the
// tools/check_test_hygiene.sh Python-parser handover.
// See docs/CHANGELOG.md TICKET-DOCTEST-SKIP-ROT entry.
#include <doctest/doctest.h>
TEST_CASE("[selftest #1] DOCTEST_SKIP w/ TICKET-DOCTEST-SKIP-ROT prefix (PASS)") {
    DOCTEST_SKIP("TICKET-DOCTEST-SKIP-ROT: documentation selftest - PASS-1");
}
TEST_CASE("[selftest #2] SKIP() w/ TICKET-DOCTEST-SKIP-ROT in 3-line context (PASS)") {
    // TICKET-DOCTEST-SKIP-ROT: 3-line context selftest
    SKIP("TICKET-DOCTEST-SKIP-ROT: documentation selftest - PASS-2");
}
TEST_CASE("[selftest #3] doctest::skip(true) w/ TICKET-DOCTEST-SKIP-ROT in context (PASS)") {
    // TICKET-DOCTEST-SKIP-ROT: function-form selftest
    doctest::skip(true);
}
TEST_CASE("[selftest #4] DOCTEST_SKIP inside comment - comment-stripper (PASS)") {
    // DOCTEST_SKIP("inside comment, must be stripped by the parser")
    CHECK(true);
}
// PASS-5 invariant: file lives in tests/helpers/ - helper-dir exclusion filters it.
// FAIL-1 COMMENTED to avoid self-tripping:
// TEST_CASE("[selftest FAIL-1] bare DOCTEST_SKIP no TICKET- (FAIL)") {
//     DOCTEST_SKIP("no TICKET- ref - would emit HYGIENE_FAIL");
// }
