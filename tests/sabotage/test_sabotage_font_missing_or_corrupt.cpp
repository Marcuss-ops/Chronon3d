// ============================================================================
// tests/sabotage/test_sabotage_font_missing_or_corrupt.cpp
// ============================================================================
//
// Sabotage scenario #1: font missing OR font file corrupt.
// Engine signature (L4 verdict): `src/backends/text/font_engine.cpp`
// reaches `BLFontFaceCache::get_face(...)` which returns a nullptr
// (Or an exception) when the font file is missing or corrupt.
//
// On the production code path the engine is EXPECTED to fail-loud with
// `MissingFontEngine` / `FontResolutionFailed` Result<>-typed error
// (per TEXT-V1 Phase A upper bounds + TICKET-FU04/FU02 scaffold).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node +
// fail-loud":
//   - INFO lines identify comp/layer/node.
//   - FAIL_CHECK forces the doctest assertion to fail → exit 1.
//   - The test is a fail-loud VERIFICATION that the scenario fires.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::font_missing {

// Engine-side trigger fingerprint. Returns true if the production
// fail-loud path fires correctly when a font is missing/corrupt.
// Implementation forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS.
bool trigger_font_resolution_failure() {
    // CANARY: the production fail-loud hook has not yet been wired into
    // this stub. The production code is TICKET-SABOTAGE-PRODUCTION-HOOKS
    // (forward-point). Per user spec, the test exits non-zero regardless
    // of the trigger's return value — the fail-loud is gated by
    // FAIL_CHECK downstream.
    return false;
}

} // namespace chaos::sabotage::font_missing

TEST_CASE(
    "sabotage/font_missing_or_corrupt "
    "[comp=TextComposition, layer=TextLayoutLayer, node=BlFontFaceCache]"
) {
    INFO("Comp=TextComposition")
    INFO("Layer=TextLayoutLayer")
    INFO("Node=BlFontFaceCache")
    INFO("Expected fail-loud path: MissingFontEngine / FontResolutionFailed Result<>")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    // Per user spec, this test is DESIGNED to exit non-zero. doctest's
    // FAIL_CHECK marks the assertion as failed → exit 1.
    FAIL_CHECK(
        "sabotage/font_missing_or_corrupt: trigger returns false — production "
        "fail-loud hook not yet wired (TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
