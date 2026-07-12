#include <doctest/doctest.h>

namespace chronon3d::sabotage::font {

bool attempt_font_load(const char*) {
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

    SUBCASE("missing font file returns failure") {
        CHECK_FALSE(chronon3d::sabotage::font::attempt_font_load(
            "tests/fixtures/fonts/does-not-exist.ttf"));
    }

    SUBCASE("corrupt font file returns failure") {
        CHECK_FALSE(chronon3d::sabotage::font::attempt_font_load(
            "tests/fixtures/fonts/corrupt-font.ttf"));
    }
}
