// tests/sabotage/test_sabotage_font_missing_or_corrupt.cpp
// ============================================================================
// Sabotage scenario: font missing OR font file corrupt.
//
// Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS — when the production
// fail-loud hooks are wired, replace attempt_font_load() with the real
// FontEngine::load_font() call and add error-code assertions:
//   REQUIRE(result.error().code == TextErrorCode::FontNotFound);
//   REQUIRE(result.error().code == TextErrorCode::FontLoadFailed
//        || result.error().code == TextErrorCode::InvalidFont);
// ============================================================================
#include <doctest/doctest.h>

namespace chronon3d::sabotage::font {

// Stub: simulates a font resolution attempt.
// Returns true if the font was loaded, false on failure.
// Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS — replace with
// real FontEngine::load_font() call.
bool attempt_font_load(const char* /*font_path*/) {
    return false;
}

} // namespace chronon3d::sabotage::font

TEST_CASE(
    "sabotage/font_missing_or_corrupt: missing font fails loudly "
    "[comp=TextComposition][layer=TextLayoutLayer][node=BlFontFaceCache]"
) {
    INFO("Comp=TextComposition");
    INFO("Layer=TextLayoutLayer");
    INFO("Node=BlFontFaceCache");
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS");

    SUBCASE("missing font file returns failure") {
        REQUIRE_FALSE(chronon3d::sabotage::font::attempt_font_load(
            "tests/fixtures/fonts/does-not-exist.ttf"));
    }

    SUBCASE("corrupt font file returns failure") {
        REQUIRE_FALSE(chronon3d::sabotage::font::attempt_font_load(
            "tests/fixtures/fonts/corrupt-font.ttf"));
    }
}
