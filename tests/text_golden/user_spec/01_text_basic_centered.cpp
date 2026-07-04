// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/01_text_basic_centered.cpp
//
// ADR-014 SKELETON (deferred to follow-up commit).
//
// Stub: this file is committed in the doc-only scope per the user's
// two-phase commit decision. The polished scene-builder implementation
// (composition{...} + SceneBuilder + LayerBuilder::text + verify_golden)
// and the matching golden PNG arrive in the ADR-014 follow-up commit
// alongside the rest of the 12 user-spec tests. Spec intent is documented
// in docs/adr/ADR-014-text-golden-coverage.md, Decision 1 row #1.
//
// Why: tests/text_golden_tests.cmake uses target_sources() to list each
// of the 12 user-spec files. Keeping this file as a trivial doctest stub
// preserves cmake target validity on a fresh clone while deferring the
// scene-builder implementation until API surface is verified end-to-end.
//
// CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGoldenUserSpec will capture the
// actual golden PNG once the follow-up commit replaces this skeleton.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

TEST_CASE("UserSpec 01: text basic centered — SKELETON (deferred to ADR-014 follow-up commit)") {
    MESSAGE("ADR-014 skeleton placeholder — replace with the Inter-Bold 96px centered smoke test in the ADR-014 follow-up commit.");
    CHECK(true);
}
