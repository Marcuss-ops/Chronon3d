// ============================================================================
// tests/sabotage/test_sabotage_glyph_count_zero.cpp
// ============================================================================
//
// Sabotage scenario #2: text layout returns `glyph_count == 0` for a
// non-empty `content` input (the layout engine successfully resolved
// a font + glyph count fallback that produces no glyphs — e.g. empty
// string after emoji stripping, or harvest_glyphs() short-circuit on
// whitespace-only input).
//
// Engine signature (verified-real surface): `glyph_count` member
// exists across `include/chronon3d/text/composer_types.hpp`
// + `include/chronon3d/text/text_unit_map.hpp` + `include/chronon3d/
// text/text_visibility_audit.hpp` (machine-verified via grep 2026-
// 07-12). The Phase 2 fail-loud HOOK signature is PLANNED
// (TICKET-SABOTAGE-PRODUCTION-HOOKS): `trigger_glyph_count_zero_
// failure()` returns false, replaced by the real production hook in
// Phase 2.
//
// Expected fail-loud path: EmptyGlyphRun Result<> error + DOES NOT
// silently produce a zero-glyph layout (which downstream glyph-cache
// + render-path interpret as "valid empty render", causing flicker
// + invisible-text rot-class FU05).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels — actual
//     production symbol mapping is forward-pointed to
//     TICKET-SABOTAGE-PRODUCTION-HOOKS Phase 2).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
//   - The test is a fail-loud VERIFICATION that the scenario fires
//     on the production fail-loud hook once the hook is wired in.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::glyph_count_zero {

// Engine-side trigger fingerprint. Returns true if the production
// fail-loud path fires correctly when glyph_count == 0 on a
// non-empty content input. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — the production hook is PLANNED
// (no real `trigger_glyph_count_zero_failure()` symbol exists yet;
// the production-hook will read the verified-real `glyph_count`
// member surfaced at include/chronon3d/text/composer_types.hpp
// + text_unit_map.hpp + text_visibility_audit.hpp).
bool trigger_glyph_count_zero_failure() {
    // CANARY: the production fail-loud hook has not yet been wired
    // into this stub. The production code is TICKET-SABOTAGE-
    // PRODUCTION-HOOKS (forward-point). Per user spec, the test
    // exits non-zero regardless of the trigger's return value; the
    // fail-loud is gated by FAIL_CHECK downstream.
    return false;
}

} // namespace chaos::sabotage::glyph_count_zero

TEST_CASE(
    "sabotage/glyph_count_zero "
    "[comp=TextComposition, layer=TextLayoutLayer, "
    "node=glyph_count_RESULT_eq_zero_PLANNED]"
) {
    INFO("Comp=TextComposition")
    INFO("Layer=TextLayoutLayer [PLANNED TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Node=glyph_count=0 on non-empty content [verified-real at "
         "composer_types.hpp/text_unit_map.hpp/text_visibility_audit.hpp]")
    INFO("Sabotage scenario: layout(content) returns glyph_count=0 "
         "for non-empty content -> EmptyGlyphRun Result<>")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    // Per user spec, this test is DESIGNED to exit non-zero.
    // doctest's FAIL_CHECK marks the assertion as failed -> exit 1.
    FAIL_CHECK(
        "sabotage/glyph_count_zero: trigger returns false -- production "
        "fail-loud hook not yet wired (TICKET-SABOTAGE-PRODUCTION-HOOKS "
        "forward-point)"
    );
}
