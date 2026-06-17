#include <doctest/doctest.h>
#include <chronon3d/text/font_engine.hpp>
#include <cmath>
#include <vector>
#include <string>
using namespace chronon3d;


static FontSpec inter_bold() {
    return FontSpec{
        .font_path = "assets/fonts/Inter-Bold.ttf",
        .font_family = "Inter",
        .font_weight = 700,
    };
}

static FontSpec poppins_bold() {
    return FontSpec{
        .font_path = "assets/fonts/Poppins-Bold.ttf",
        .font_family = "Poppins",
        .font_weight = 700,
    };
}

TEST_CASE("FontEngine: HarfBuzz linear proportionality across font sizes") {
    // Verify that shaping widths scale LINEARLY with font size.
    // If font_unit_scale() is wrong (e.g. double-applied after
    // hb_ft_font_changed), width would grow quadratically:
    //   width ∝ font_size²   →   width_64/width_32 ≈ 4.0  (BUG!)
    // Correct linear scaling:
    //   width ∝ font_size    →   width_64/width_32 ≈ 2.0
    //
    // Test on Poppins-Bold with multiple strings to avoid
    // coincidental passing from a single glyph.

    FontEngine engine;
    const FontSpec font = poppins_bold();

    struct TestCase { std::string text; };
    std::vector<TestCase> cases = {
        {"Hello"},
        {"Minimum"},       // many vertical strokes, sensitive to advance errors
        {"AV"},             // classic kerning pair
        {"The quick brown fox"},  // multi-word sentence
    };

    // Tolerance: 5% allows for hinting differences at small sizes
    // but rejects the 100% error (ratio ≈ 4.0) from quadratic scaling.
    const double kEpsilon = 0.05;

    for (const auto& tc : cases) {
        float w16 = engine.measure_text(tc.text, font, 16.0f);
        float w32 = engine.measure_text(tc.text, font, 32.0f);
        float w64 = engine.measure_text(tc.text, font, 64.0f);

        INFO("Text: '", tc.text, "'");
        REQUIRE(w16 > 0.0f);
        REQUIRE(w32 > 0.0f);
        REQUIRE(w64 > 0.0f);

        // ratio_32_16 = w32 / w16 should be ≈ 2.0 (not 4.0!)
        float ratio_32_16 = w32 / w16;
        CHECK(ratio_32_16 == doctest::Approx(2.0f).epsilon(kEpsilon));

        // ratio_64_32 = w64 / w32 should be ≈ 2.0
        float ratio_64_32 = w64 / w32;
        CHECK(ratio_64_32 == doctest::Approx(2.0f).epsilon(kEpsilon));
    }
}

TEST_CASE("FontEngine: HarfBuzz glyph advance is pixel-correct at known size") {
    // Sanity check: at 100px, a capital 'M' (typically widest glyph)
    // should have advance between 50-120px on Poppins-Bold.
    // This guards against orders-of-magnitude errors in the scale factor.

    FontEngine engine;
    const FontSpec font = poppins_bold();

    auto run = engine.shape_text("M", font, 100.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    float advance = run->glyphs[0].advance_x;
    INFO("M advance at 100px: ", advance);
    CHECK(advance > 50.0f);
    CHECK(advance < 120.0f);

    // Run the same at 50px and check ratio
    auto run2 = engine.shape_text("M", font, 50.0f);
    REQUIRE(run2.has_value());
    float advance_50 = run2->glyphs[0].advance_x;

    float ratio = advance / advance_50;
    INFO("M advance ratio 100/50: ", ratio);
    CHECK(ratio == doctest::Approx(2.0f).epsilon(0.05));
}

TEST_CASE("FontEngine: can load Inter-Bold") {
    FontEngine engine;
    CHECK(engine.can_load(inter_bold()));
}

TEST_CASE("FontEngine: cannot load missing font") {
    FontEngine engine;
    FontSpec missing{.font_path = "nonexistent_font.ttf", .font_family = "Missing"};
    CHECK_FALSE(engine.can_load(missing));
}

TEST_CASE("FontEngine: shape_text returns glyph positions") {
    FontEngine engine;
    auto run = engine.shape_text("Hi", inter_bold(), 32.0f);
    REQUIRE(run.has_value());
    CHECK(run->glyphs.size() > 0);
    CHECK(run->width > 0.0f);
    CHECK(run->ascent > 0.0f);
    CHECK(run->line_height > 0.0f);
}

TEST_CASE("FontEngine: shape_text empty string returns nullopt") {
    FontEngine engine;
    auto run = engine.shape_text("", inter_bold(), 32.0f);
    CHECK_FALSE(run.has_value());
}

TEST_CASE("FontEngine: shape_text invalid font returns nullopt") {
    FontEngine engine;
    FontSpec missing{.font_path = "missing.ttf", .font_family = "X"};
    auto run = engine.shape_text("Hi", missing, 32.0f);
    CHECK_FALSE(run.has_value());
}

TEST_CASE("FontEngine: measure_text returns positive width for text") {
    FontEngine engine;
    float w = engine.measure_text("Hello World", inter_bold(), 32.0f);
    CHECK(w > 0.0f);
}

TEST_CASE("FontEngine: measure_text returns 0 for missing font") {
    FontEngine engine;
    FontSpec missing{.font_path = "missing.ttf", .font_family = "X"};
    float w = engine.measure_text("Hello", missing, 32.0f);
    CHECK(w == 0.0f);
}

TEST_CASE("FontEngine: larger font size gives larger width") {
    FontEngine engine;
    float w16 = engine.measure_text("Hello", inter_bold(), 16.0f);
    float w32 = engine.measure_text("Hello", inter_bold(), 32.0f);
    float w64 = engine.measure_text("Hello", inter_bold(), 64.0f);
    REQUIRE(w16 > 0.0f);
    REQUIRE(w32 > 0.0f);
    REQUIRE(w64 > 0.0f);
    CHECK(w32 > w16 * 1.5f);  // at least 1.5x bigger
    CHECK(w64 > w32 * 1.5f);
}

TEST_CASE("FontEngine: get_font_metrics returns valid values") {
    FontEngine engine;
    auto metrics = engine.get_font_metrics(inter_bold(), 32.0f);
    CHECK(metrics.ascent > 0.0f);
    CHECK(metrics.descent >= 0.0f);
    CHECK(metrics.line_height > 0.0f);
    CHECK(metrics.cap_height > 0.0f);
    CHECK(metrics.max_advance > 0.0f);
}

TEST_CASE("FontEngine: glyph run width equals sum of advances") {
    FontEngine engine;
    auto run = engine.shape_text("AB", inter_bold(), 32.0f);
    REQUIRE(run.has_value());
    float sum_adv = 0.0f;
    for (const auto& g : run->glyphs) {
        sum_adv += g.advance_x;
    }
    CHECK(run->width == doctest::Approx(sum_adv).epsilon(0.5f));
}

TEST_CASE("FontEngine: shape_text kerning differs from unshaped width") {
    FontEngine engine;
    // "AV" is a classic kerning pair in many fonts
    auto run_av = engine.shape_text("AV", inter_bold(), 32.0f);
    auto run_a = engine.shape_text("A", inter_bold(), 32.0f);
    auto run_v = engine.shape_text("V", inter_bold(), 32.0f);

    REQUIRE(run_av.has_value());
    REQUIRE(run_a.has_value());
    REQUIRE(run_v.has_value());

    float separate = run_a->width + run_v->width;
    float together = run_av->width;

    // With proper HarfBuzz shaping, AV together should be <= separate (kerning reduces width)
    // Some fonts may not kern AV strongly; allow equality.
    CHECK(together <= separate);
}

TEST_CASE("FontEngine: global shape_text convenience function works") {
    auto run = chronon3d::shape_text("Test", inter_bold(), 24.0f);
    REQUIRE(run.has_value());
    CHECK(run->glyphs.size() > 0);
    CHECK(run->width > 0.0f);
}

TEST_CASE("FontEngine: cache persists across calls") {
    FontEngine engine;
    auto m1 = engine.get_font_metrics(inter_bold(), 32.0f);
    auto m2 = engine.get_font_metrics(inter_bold(), 32.0f);
    CHECK(m1.ascent == m2.ascent);
    CHECK(m1.descent == m2.descent);
}

TEST_CASE("FontEngine: glyph bbox cache populates on shape_text") {
    FontEngine engine;
    CHECK(engine.glyph_bbox_cache_size() == 0);

    auto run = engine.shape_text("AB", inter_bold(), 32.0f);
    REQUIRE(run.has_value());
    CHECK(engine.glyph_bbox_cache_size() > 0);

    // Every glyph should have a non-zero bounding box
    for (const auto& g : run->glyphs) {
        CHECK(g.bbox_x0 < g.bbox_x1);
        CHECK(g.bbox_y1 < g.bbox_y0);  // y grows downward in FT coords
    }
}

TEST_CASE("FontEngine: glyph bbox cache grows with unique glyphs") {
    FontEngine engine;
    auto run1 = engine.shape_text("ABC", inter_bold(), 32.0f);
    REQUIRE(run1.has_value());
    size_t after_first = engine.glyph_bbox_cache_size();
    CHECK(after_first > 0);

    // Same glyphs, same size → cache should not grow
    auto run2 = engine.shape_text("CBA", inter_bold(), 32.0f);
    REQUIRE(run2.has_value());
    CHECK(engine.glyph_bbox_cache_size() == after_first);

    // Different size → new cache entries
    auto run3 = engine.shape_text("AB", inter_bold(), 64.0f);
    REQUIRE(run3.has_value());
    CHECK(engine.glyph_bbox_cache_size() > after_first);
}

TEST_CASE("FontEngine: glyph bbox cache cleared with clear_cache") {
    FontEngine engine;
    auto run = engine.shape_text("X", inter_bold(), 32.0f);
    REQUIRE(run.has_value());
    CHECK(engine.glyph_bbox_cache_size() > 0);

    engine.clear_cache();
    CHECK(engine.glyph_bbox_cache_size() == 0);
}
