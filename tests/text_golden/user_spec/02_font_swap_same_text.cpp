// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/02_font_swap_same_text.cpp
//
// ADR-014 SKELETON (deferred to follow-up commit).
//
// Stub for the font-swap-same-text cache-invalidation gate (spec row #2
// in ADR-014 Decision 1). The polished implementation renders "SAME TEXT"
// twice — Bold (Frame 0) and Regular (Frame 30) — and verifies that
// TextLayoutCacheKey correctly invalidates on font_family change.
//
// See docs/adr/ADR-014-text-golden-coverage.md for spec.
// Replace this skeleton in the ADR-014 follow-up commit.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

TEST_CASE("UserSpec 02: font swap same text — SKELETON (deferred to ADR-014 follow-up commit)") {
    MESSAGE("ADR-014 skeleton placeholder — replace with frame 0 + frame 30 cache-invalidation test in the ADR-014 follow-up commit.");
    CHECK(true);
}
