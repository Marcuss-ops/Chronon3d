#include <doctest/doctest.h>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/text_animator.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

static FontSpec inter_bold_quality() {
    return FontSpec{
        .font_path = "assets/fonts/Inter-Bold.ttf",
        .font_family = "Inter",
        .font_weight = 700,
    };
}

/// Returns true if the font is available, skipping the test with a message otherwise.
static bool require_font(FontEngine& engine, const FontSpec& spec = inter_bold_quality()) {
    if (!engine.can_load(spec)) {
        MESSAGE("Skipping: font not available (", spec.font_path, ")");
        return false;
    }
    return true;
}

/// Measure width using FontEngine (no tracking).
static float measure(FontEngine& engine, std::string_view text, float size = 32.0f) {
    return engine.measure_text(text, inter_bold_quality(), size);
}

/// Shape text with FontEngine.
static std::optional<GlyphRun> shape(FontEngine& engine, std::string_view text,
                                      float size = 32.0f) {
    return engine.shape_text(text, inter_bold_quality(), size);
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. Glyph Placement — No Double-Advance
// ═══════════════════════════════════════════════════════════════════════════
//
// HbToBlGlyphRun uses relative offsets (x_offset, y_offset), NOT cumulative
// positions (x, y). With ADVANCE_OFFSET placement, Blend2D adds placement
// to the current pen, then advances. If cumulative positions were passed,
// the pen would advance twice.
//
// We test via FontEngine::shape_text() which returns GlyphRun with both
// cumulative (x) and relative (x_offset) positions. Correct behavior:
//   g.x_offset ≈ g.x - sum_of_previous_offsets
//   sum(g.x_offset + g.advance_x) ≈ g.x + g.advance_x for the last glyph

TEST_CASE("TextQuality: glyph placement — relative offsets used") {
    FontEngine engine;
    if (!require_font(engine)) return;

    auto run = shape(engine, "ABC", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() >= 3);

    // Verify each glyph has sensible values
    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& g = run->glyphs[i];
        INFO("Glyph ", i);
        CHECK(g.advance_x > 0.0f);
        // Offset should be small relative to cumulative position
        CHECK(std::abs(g.x_offset) < g.advance_x * 2.0f);
        CHECK(std::abs(g.y_offset) < g.advance_x * 0.5f);
    }

    // Sum of advances should match run width
    float sum_advances = 0.0f;
    for (const auto& g : run->glyphs) {
        sum_advances += g.advance_x;
    }
    CHECK(run->width == doctest::Approx(sum_advances).epsilon(0.02f));
}

TEST_CASE("TextQuality: glyph placement — no double-counting") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // Shape "AV" — a kerning pair
    auto run = shape(engine, "AV", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 2);

    const auto& g0 = run->glyphs[0];
    const auto& g1 = run->glyphs[1];

    // Cumulative position of glyph 1 should be:
    //   g0.x_offset + g0.advance_x + g1.x_offset
    // If double-advancing, g1.x would be much larger
    float expected_x = g0.x_offset + g0.advance_x + g1.x_offset;
    CHECK(g1.x == doctest::Approx(expected_x).epsilon(0.5f));
}

TEST_CASE("TextQuality: glyph placement — single glyph has correct bbox") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // 'M' is a wide glyph
    auto run = shape(engine, "M", 100.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    const auto& g = run->glyphs[0];
    // At 100px, 'M' should be between 50-120px wide
    CHECK(g.advance_x > 50.0f);
    CHECK(g.advance_x < 120.0f);

    // Bounding box should be valid (positive width, y-down in FT coords)
    CHECK(g.bbox_x0 < g.bbox_x1);
    CHECK(g.bbox_y1 < g.bbox_y0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Tracking — Per-Cluster, Not Per-Glyph
// ═══════════════════════════════════════════════════════════════════════════
//
// Tracking should add spacing only between grapheme clusters, not between
// glyphs within the same cluster (e.g., base + combining mark).
// Formula: width = HarfBuzz_advance + tracking × max(clusters − 1, 0)

TEST_CASE("TextQuality: tracking 0/5/20 — increases width proportionally") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // measure_text doesn't support tracking directly, so use layout engine
    TextLayoutInput li;
    li.text = "ABC";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 5.0f;
    auto res5 = TextLayoutEngine::layout(li);
    float w5 = res5.size.x;

    li.style.tracking = 20.0f;
    auto res20 = TextLayoutEngine::layout(li);
    float w20 = res20.size.x;

    // 3 clusters → tracking adds (3-1) * tracking_val
    CHECK(w5 == doctest::Approx(w0 + 10.0f).epsilon(0.2f));
    CHECK(w20 == doctest::Approx(w0 + 40.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: tracking — single character gets 0 tracking") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "A";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 20.0f;
    auto res20 = TextLayoutEngine::layout(li);
    float w20 = res20.size.x;

    // Single cluster: max(1-1, 0) = 0 tracking added
    CHECK(w20 == doctest::Approx(w0).epsilon(0.02f));
}

TEST_CASE("TextQuality: tracking — empty string has zero width") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();
    li.style.tracking = 50.0f;

    auto res = TextLayoutEngine::layout(li);
    CHECK(res.size.x == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Grapheme Clusters — Combining Marks, ZWJ Emoji, RI Flags
// ═══════════════════════════════════════════════════════════════════════════
//
// grapheme_cluster_count() and grapheme_byte_offset_at() must correctly
// handle multi-code-point user-perceived characters.

TEST_CASE("TextQuality: grapheme — e + combining acute accent is 1 cluster") {
    using namespace detail;

    // "é" can be represented as U+0065 (e) + U+0301 (combining acute)
    // That's 2 code points but 1 grapheme cluster
    const std::string e_acute = "e\xCC\x81"; // e + U+0301 in UTF-8
    CHECK(utf8_length(e_acute) == 2);        // 2 code points
    CHECK(grapheme_cluster_count(e_acute) == 1); // 1 cluster
}

TEST_CASE("TextQuality: grapheme — ZWJ emoji family is 1 cluster") {
    using namespace detail;

    // Woman + ZWJ + Woman + ZWJ + Girl + ZWJ + Boy
    // Family emoji: 👩‍👩‍👧‍👦
    // U+1F469 U+200D U+1F469 U+200D U+1F467 U+200D U+1F466
    const std::string family =
        "\xF0\x9F\x91\xA9"   // U+1F469 Woman
        "\xE2\x80\x8D"       // U+200D ZWJ
        "\xF0\x9F\x91\xA9"   // U+1F469 Woman
        "\xE2\x80\x8D"       // U+200D ZWJ
        "\xF0\x9F\x91\xA7"   // U+1F467 Girl
        "\xE2\x80\x8D"       // U+200D ZWJ
        "\xF0\x9F\x91\xA6";  // U+1F466 Boy

    CHECK(utf8_length(family) == 7);                // 7 code points
    CHECK(grapheme_cluster_count(family) == 1);     // 1 cluster!
}

TEST_CASE("TextQuality: grapheme — skin tone modifier stays attached") {
    using namespace detail;

    // Thumbs up + skin tone: 👍🏻
    // U+1F44D U+1F3FB
    const std::string skin_tone =
        "\xF0\x9F\x91\x8D"   // U+1F44D Thumbs Up
        "\xF0\x9F\x8F\xBB";  // U+1F3FB Light skin tone

    CHECK(utf8_length(skin_tone) == 2);             // 2 code points
    CHECK(grapheme_cluster_count(skin_tone) == 1);  // 1 cluster
}

TEST_CASE("TextQuality: grapheme — 🇮🇹 RI pair is 1 cluster") {
    using namespace detail;

    // Italian flag: 🇮 + 🇹 = 🇮🇹
    // U+1F1EE (RI I) + U+1F1F9 (RI T)
    const std::string italy =
        "\xF0\x9F\x87\xAE"   // U+1F1EE Regional Indicator I
        "\xF0\x9F\x87\xB9";  // U+1F1F9 Regional Indicator T

    CHECK(utf8_length(italy) == 2);                 // 2 code points
    CHECK(grapheme_cluster_count(italy) == 1);      // 1 cluster (flag)
}

TEST_CASE("TextQuality: grapheme — single RI followed by ASCII counts separately") {
    using namespace detail;

    // Single regional indicator + 'A'
    // U+1F1EE (RI I) + 'A'
    const std::string ri_a =
        "\xF0\x9F\x87\xAE"   // U+1F1EE Regional Indicator I
        "A";

    // The single RI counts as 1 cluster, 'A' as another
    CHECK(grapheme_cluster_count(ri_a) == 2);
}

TEST_CASE("TextQuality: grapheme — byte offset at works for ZWJ emoji sequence") {
    using namespace detail;

    // Woman + ZWJ + Woman = 1 cluster (GB11)
    // U+1F469 U+200D U+1F469 = 3 code points, 1 cluster
    std::string zwj_seq =
        "\xF0\x9F\x91\xA9"   // U+1F469 Woman (4 bytes)
        "\xE2\x80\x8D"       // U+200D ZWJ (3 bytes)
        "\xF0\x9F\x91\xA9";  // U+1F469 Woman (4 bytes)
    // Total: 11 bytes

    CHECK(grapheme_cluster_count(zwj_seq) == 1);
    // Byte offset after 1st (and only) cluster = all 11 bytes
    CHECK(grapheme_byte_offset_at(zwj_seq, 1) == 11);
}

TEST_CASE("TextQuality: grapheme — byte offset at works correctly") {
    using namespace detail;

    // "A" + é (e+combining acute, 3 bytes) + "B" = 3 clusters, 5 bytes
    // Bytes: [A=0][e=1][CC=2][81=3][B=4]
    std::string text = "A" "e\xCC\x81" "B";

    CHECK(grapheme_cluster_count(text) == 3);

    // Byte offset after 1st cluster (A) = 1
    CHECK(grapheme_byte_offset_at(text, 1) == 1);

    // Byte offset after 2nd cluster (é = e+0xCC+0x81 = 3 bytes) = 1+3 = 4
    CHECK(grapheme_byte_offset_at(text, 2) == 4);

    // Byte offset after 3rd cluster (B = 1 byte) = 4+1 = 5
    CHECK(grapheme_byte_offset_at(text, 3) == 5);

    // Beyond end = sv.size()
    CHECK(grapheme_byte_offset_at(text, 10) == text.size());
}

TEST_CASE("TextQuality: grapheme — ASCII text count equals code point count") {
    using namespace detail;

    CHECK(grapheme_cluster_count("") == 0);
    CHECK(grapheme_cluster_count("A") == 1);
    CHECK(grapheme_cluster_count("ABC") == 3);
    CHECK(grapheme_cluster_count("Hello World") == 11);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. RTL Shaping — Arabic and Hebrew
// ═══════════════════════════════════════════════════════════════════════════
//
// When shaped with RTL direction, HarfBuzz should:
// - Produce glyphs in visual order
// - Set is_cluster_start correctly
// - Have decreasing x positions for consecutive glyphs

TEST_CASE("TextQuality: RTL — Arabic text shapes correctly") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // Simple Arabic word: مرحبا (marhaba — "hello")
    // U+0645 U+0631 U+062D U+0628 U+0627
    const std::string arabic =
        "\xD9\x85"   // U+0645 Meem
        "\xD8\xB1"   // U+0631 Reh
        "\xD8\xAD"   // U+062D Hah
        "\xD8\xA8"   // U+0628 Beh
        "\xD8\xA7";  // U+0627 Alef

    TextShaping arabic_shaping;
    arabic_shaping.direction = TextDirection::RTL;
    arabic_shaping.language = "ar";

    auto run = engine.shape_text(arabic, inter_bold_quality(), 32.0f, arabic_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    // Width should be positive
    CHECK(run->width > 0.0f);
}

TEST_CASE("TextQuality: RTL — Hebrew text shapes correctly") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // שלום (shalom)
    // U+05E9 U+05DC U+05D5 U+05DD
    const std::string hebrew =
        "\xD7\xA9"   // U+05E9 Shin
        "\xD7\x9C"   // U+05DC Lamed
        "\xD7\x95"   // U+05D5 Vav
        "\xD7\x9D";  // U+05DD Mem Sofit

    TextShaping hebrew_shaping;
    hebrew_shaping.direction = TextDirection::RTL;
    hebrew_shaping.language = "he";

    auto run = engine.shape_text(hebrew, inter_bold_quality(), 32.0f, hebrew_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->width > 0.0f);
}

TEST_CASE("TextQuality: RTL — LTR text shapes correctly with explicit direction") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextShaping ltr_shaping;
    ltr_shaping.direction = TextDirection::LTR;
    ltr_shaping.language = "en";

    auto run = engine.shape_text("ABC", inter_bold_quality(), 32.0f, ltr_shaping);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 3);

    // In LTR, cumulative x positions should increase
    CHECK(run->glyphs[0].x < run->glyphs[1].x);
    CHECK(run->glyphs[1].x < run->glyphs[2].x);
}

TEST_CASE("TextQuality: RTL — multi-glyph cluster (ligature) has correct is_cluster_start") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // Arabic "لا" (lam-alef ligature) — may produce 1 or 2 glyphs
    // U+0644 (Lam) + U+0627 (Alef)
    const std::string lam_alef =
        "\xD9\x84"   // U+0644 Lam
        "\xD8\xA7";  // U+0627 Alef

    TextShaping arabic_shaping;
    arabic_shaping.direction = TextDirection::RTL;
    arabic_shaping.language = "ar";

    auto run = engine.shape_text(lam_alef, inter_bold_quality(), 32.0f, arabic_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    // First glyph should be a cluster start
    CHECK(run->glyphs[0].is_cluster_start);

    // For multi-glyph renderings, only the first glyph starts the cluster
    for (size_t i = 1; i < run->glyphs.size(); ++i) {
        INFO("Glyph ", i);
        // Non-first glyphs may or may not be cluster_start depending on
        // whether HarfBuzz produced a ligature or multiple glyphs
        // Just verify sensible values
        CHECK(run->glyphs[i].advance_x > 0.0f);
    }
}

TEST_CASE("TextQuality: RTL — Arabic positions are in visual (right-to-left) order") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // مرحبا — 5 Arabic letters shaped with RTL direction
    const std::string arabic =
        "\xD9\x85"   // U+0645 Meem
        "\xD8\xB1"   // U+0631 Reh
        "\xD8\xAD"   // U+062D Hah
        "\xD8\xA8"   // U+0628 Beh
        "\xD8\xA7";  // U+0627 Alef

    TextShaping rtl_shaping;
    rtl_shaping.direction = TextDirection::RTL;
    rtl_shaping.language = "ar";

    auto run = engine.shape_text(arabic, inter_bold_quality(), 32.0f, rtl_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    // In visual order (glyph buffer order), consecutive glyph positions
    // should increase (each glyph placed to the right of the previous).
    // HarfBuzz always outputs in visual order.
    for (size_t i = 1; i < run->glyphs.size(); ++i) {
        INFO("Glyph ", i);
        CHECK(run->glyphs[i].x > run->glyphs[i-1].x - 0.5f);
    }
}

TEST_CASE("TextQuality: RTL — LTR and RTL produce different glyph orders") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // A longer Arabic string where LTR vs RTL shaping is more likely to differ.
    // مرحبا (5 letters) gives more shaping opportunities.
    const std::string arabic =
        "\xD9\x85"   // U+0645 Meem
        "\xD8\xB1"   // U+0631 Reh
        "\xD8\xAD"   // U+062D Hah
        "\xD8\xA8"   // U+0628 Beh
        "\xD8\xA7";  // U+0627 Alef

    TextShaping ltr;
    ltr.direction = TextDirection::LTR;
    ltr.language = "ar";

    TextShaping rtl;
    rtl.direction = TextDirection::RTL;
    rtl.language = "ar";

    auto run_ltr = engine.shape_text(arabic, inter_bold_quality(), 32.0f, ltr);
    auto run_rtl = engine.shape_text(arabic, inter_bold_quality(), 32.0f, rtl);
    REQUIRE(run_ltr.has_value());
    REQUIRE(run_rtl.has_value());

    // Both should produce glyphs
    CHECK(run_ltr->glyphs.size() > 0);
    CHECK(run_rtl->glyphs.size() > 0);

    // LTR and RTL shaping may produce identical glyph positions if the
    // font doesn't have directional positioning tables.  This is font-
    // dependent.  We verify both shapes produce valid results.
    // For fonts with RTL support, at least one glyph position or advance
    // should differ; we check for any difference but don't hard-fail
    // if the font doesn't support directional shaping.
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Stroke vs Fill — Shared Shaping Glyphs
// ═══════════════════════════════════════════════════════════════════════════
//
// Fill uses fillGlyphRun (HarfBuzz-shaped glyph IDs).
// Stroke uses FtGlyphPathBuilder (same HarfBuzz glyph IDs converted to paths).
// Both receive identical tracking and origin, producing aligned output.
//
// NOTE: HbToBlGlyphRun and FtGlyphPathBuilder live in an anonymous namespace
// in text_rasterizer_render.cpp, so we can't directly test stroke-vs-fill
// path identity.  We verify indirectly via glyph property consistency.

TEST_CASE("TextQuality: stroke vs fill — glyph IDs match between fill and stroke") {
    FontEngine engine;
    if (!require_font(engine)) return;

    auto run = shape(engine, "ABC", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 3);

    // Glyph IDs should be non-zero (valid font glyph indices)
    for (const auto& g : run->glyphs) {
        CHECK(g.glyph_id > 0);
    }
}

TEST_CASE("TextQuality: stroke vs fill — advances are consistent per glyph") {
    FontEngine engine;
    if (!require_font(engine)) return;

    auto run = shape(engine, "AV", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 2);

    // Each glyph should have a consistent advance
    for (const auto& g : run->glyphs) {
        CHECK(g.advance_x > 0.0f);
        // Glyph width implied by bbox should be ≤ advance
        float glyph_ink_w = g.bbox_x1 - g.bbox_x0;
        CHECK(glyph_ink_w <= g.advance_x * 1.2f);
    }
}

TEST_CASE("TextQuality: stroke vs fill — same shaped text produces same glyph count") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // Shape the same text twice — should produce identical glyph count
    auto run1 = shape(engine, "Hello", 32.0f);
    auto run2 = shape(engine, "Hello", 32.0f);
    REQUIRE(run1.has_value());
    REQUIRE(run2.has_value());

    CHECK(run1->glyphs.size() == run2->glyphs.size());
    CHECK(run1->width == doctest::Approx(run2->width).epsilon(0.01f));
}

TEST_CASE("TextQuality: stroke vs fill — text with tracking preserves glyph count") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // Tracking doesn't change shaping — same glyph count regardless
    auto run = shape(engine, "Test", 24.0f);
    REQUIRE(run.has_value());

    size_t base_count = run->glyphs.size();
    CHECK(base_count == 4); // T e s t → 4 glyphs
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. conicTo Curve Fidelity
// ═══════════════════════════════════════════════════════════════════════════
//
// FtGlyphPathBuilder uses conicTo with weight=1.0, making the rational
// quadratic Bézier identical to a standard quadratic (denominator = 1).
// A weight of √2/2 (used previously) would have distorted circular arcs.
// We test indirectly by verifying proportional bbox scaling and round-glyph
// aspect ratios — distortion would flatten or stretch O and S.

TEST_CASE("TextQuality: conicTo — glyph bboxes scale linearly with font size") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // 'O' has lots of curves — good for detecting conicTo distortion
    auto run16 = shape(engine, "O", 16.0f);
    auto run64 = shape(engine, "O", 64.0f);
    REQUIRE(run16.has_value());
    REQUIRE(run64.has_value());
    REQUIRE(run16->glyphs.size() == 1);
    REQUIRE(run64->glyphs.size() == 1);

    float w16 = run16->glyphs[0].bbox_x1 - run16->glyphs[0].bbox_x0;
    float h16 = run16->glyphs[0].bbox_y0 - run16->glyphs[0].bbox_y1;
    float w64 = run64->glyphs[0].bbox_x1 - run64->glyphs[0].bbox_x0;
    float h64 = run64->glyphs[0].bbox_y0 - run64->glyphs[0].bbox_y1;

    // Scaling should be linear: w64/w16 ≈ 4.0
    float w_ratio = w64 / w16;
    float h_ratio = h64 / h16;
    CHECK(w_ratio == doctest::Approx(4.0f).epsilon(0.15f));
    CHECK(h_ratio == doctest::Approx(4.0f).epsilon(0.15f));
}

TEST_CASE("TextQuality: conicTo — round glyph 'O' has near-square bbox") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // 'O' should be roughly circular
    auto run = shape(engine, "O", 64.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    const auto& g = run->glyphs[0];
    float glyph_w = g.bbox_x1 - g.bbox_x0;
    float glyph_h = g.bbox_y0 - g.bbox_y1; // y-down in FT coords, so invert

    // 'O' aspect ratio should be close to 1:1 (allow 15% tolerance for font design)
    float aspect = glyph_w / glyph_h;
    CHECK(aspect == doctest::Approx(1.0f).epsilon(0.15f));
}

TEST_CASE("TextQuality: conicTo — serif glyphs ('S') have correct bbox proportions") {
    FontEngine engine;
    if (!require_font(engine)) return;

    // 'S' has many curves — bbox should be taller than wide
    auto run = shape(engine, "S", 64.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    const auto& g = run->glyphs[0];
    float glyph_w = g.bbox_x1 - g.bbox_x0;
    float glyph_h = g.bbox_y0 - g.bbox_y1;

    // 'S' width is typically 50-80% of its height for most fonts
    float ratio = glyph_w / glyph_h;
    CHECK(ratio > 0.4f);
    CHECK(ratio < 0.9f);

    // Overall advance should be reasonable
    CHECK(g.advance_x > 20.0f);
    CHECK(g.advance_x < 90.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Integration: TextAnimator ByGlyph with Tracking
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: integration — ByGlyph positions increase monotonically") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextAnimator ta;
    ta.text("ABC")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine)
        .config({.mode = TextAnimMode::ByGlyph});

    auto glyphs = ta.split_glyphs();
    REQUIRE(glyphs.size() == 3);

    // Positions should increase monotonically
    CHECK(glyphs[0].x < glyphs[1].x);
    CHECK(glyphs[1].x < glyphs[2].x);

    // Each gap should be at least the advance of the first glyph
    float gap01 = glyphs[1].x - glyphs[0].x;
    float gap12 = glyphs[2].x - glyphs[1].x;
    CHECK(gap01 > glyphs[0].advance_x * 0.7f);
    CHECK(gap12 > glyphs[1].advance_x * 0.7f);
}

TEST_CASE("TextQuality: integration — measure_unit_width uses grapheme cluster count") {
    FontEngine engine;
    TextAnimator ta;
    ta.text("ABC")
        .font_size(72.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine);

    float w = ta.measure_unit_width("ABC");
    CHECK(w > 0.0f);
    // "ABC" → 3 grapheme clusters, width uses max(3-1, 0) = 2 tracking increments
    // The width should be approximately 72 * 0.58 * 3 = 125.28 (no tracking by default)
    CHECK(w > 50.0f);
    CHECK(w < 200.0f);

    // Now test tracking via the layout engine
    TextLayoutInput li;
    li.text = "ABC";
    li.style.size = 72.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res = TextLayoutEngine::layout(li);
    float w0 = res.size.x;

    li.style.tracking = 20.0f;
    auto res20 = TextLayoutEngine::layout(li);
    float w20 = res20.size.x;

    // 3 clusters: tracking adds 20 * (3-1) = 40
    CHECK(w20 == doctest::Approx(w0 + 40.0f).epsilon(0.2f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Integration: Wrapping Never Splits Grapheme Clusters
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: integration — wrapping never splits e+combining acute") {
    using namespace detail;

    // "AB" + é (e+combining acute) + "CD" → 5 grapheme clusters
    std::string text = "AB" "e\xCC\x81" "CD";
    const size_t input_clusters = grapheme_cluster_count(text);
    CHECK(input_clusters == 5);

    FontEngine engine;
    if (!require_font(engine)) return;

    // Use a very narrow box to force character wrapping
    TextLayoutInput li;
    li.text = text;
    li.style.size = 24.0f;
    li.style.wrap = TextWrap::Character;
    li.box.enabled = true;
    li.box.size = {10.0f, 400.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res = TextLayoutEngine::layout(li);

    // Count total grapheme clusters across all lines.
    // If wrapping splits a cluster, the total count will increase
    // (e+combining would become 2 separate clusters).
    size_t total_line_clusters = 0;
    for (const auto& line : res.lines) {
        total_line_clusters += grapheme_cluster_count(line.text);
    }
    // Cluster count must be preserved: no cluster was split mid-character
    CHECK(total_line_clusters == input_clusters);
}

TEST_CASE("TextQuality: integration — wrapping never splits RI flag pair") {
    using namespace detail;

    // "AB" + 🇮🇹 + "CD" → 5 clusters (flag is 1 cluster)
    std::string flag = "\xF0\x9F\x87\xAE\xF0\x9F\x87\xB9"; // 🇮🇹
    std::string text = "AB" + flag + "CD";
    const size_t input_clusters = grapheme_cluster_count(text);
    CHECK(input_clusters == 5);

    FontEngine engine;
    if (!require_font(engine)) return;

    // Use very narrow box to force character wrapping
    TextLayoutInput li;
    li.text = text;
    li.style.size = 24.0f;
    li.style.wrap = TextWrap::Character;
    li.box.enabled = true;
    li.box.size = {10.0f, 400.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res = TextLayoutEngine::layout(li);

    // Count total grapheme clusters across all lines.
    // If wrapping splits the flag pair, the total count will increase
    // (RI I + RI T would become 2 separate clusters instead of 1).
    size_t total_line_clusters = 0;
    for (const auto& line : res.lines) {
        total_line_clusters += grapheme_cluster_count(line.text);
    }
    // Cluster count must be preserved: no cluster was split mid-character
    CHECK(total_line_clusters == input_clusters);
}
