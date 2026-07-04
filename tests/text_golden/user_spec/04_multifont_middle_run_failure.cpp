// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/04_multifont_middle_run_failure.cpp
//
// ADR-014 SKELETON (deferred to follow-up commit).
//
// Stub for the middle-run PerRunShapingFailed pipeline regression lock
// (spec row #4 in ADR-014 Decision 1). The polished implementation
// either:
//   A. uses the scene-builder API to chain l.text_run() spans where
//      the middle run references a missing font; asserts the renderer
//      surfaces Err(PerRunShapingFailed) or a visible placeholder
//      instead of a silent drop; OR
//   B. delegates to the structural lock already enforced at
//      tests/text/test_compile_text_layout_errors.cpp:290 (P1-1).
//
// See docs/adr/ADR-014-text-golden-coverage.md for spec.
// Replace this skeleton in the ADR-014 follow-up commit.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

TEST_CASE("UserSpec 04: multi-font middle run failure — SKELETON (deferred to ADR-014 follow-up commit)") {
    MESSAGE("ADR-014 skeleton placeholder — replace with the PerRunShapingFailed pipeline gate (see ADR-014 Decision 1 row #4).");
    CHECK(true);
}
