#include "test_text_quality_helpers.hpp"
using namespace chronon3d;
using namespace test_text_quality;

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

    CHECK(glyphs[0].x < glyphs[1].x);
    CHECK(glyphs[1].x < glyphs[2].x);

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
    CHECK(w > 50.0f);
    CHECK(w < 200.0f);

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

    CHECK(w20 == doctest::Approx(w0 + 40.0f).epsilon(0.2f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Integration: Wrapping Never Splits Grapheme Clusters
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: integration — wrapping never splits e+combining acute") {
    using namespace detail;

    std::string text = "AB" "e\xCC\x81" "CD";
    const size_t input_clusters = grapheme_cluster_count(text);
    CHECK(input_clusters == 5);

    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = text;
    li.style.size = 24.0f;
    li.style.wrap = TextWrap::Character;
    li.box.enabled = true;
    li.box.size = {10.0f, 400.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res = TextLayoutEngine::layout(li);

    size_t total_line_clusters = 0;
    for (const auto& line : res.lines) {
        total_line_clusters += grapheme_cluster_count(line.text);
    }
    CHECK(total_line_clusters == input_clusters);
}

TEST_CASE("TextQuality: integration — wrapping never splits RI flag pair") {
    using namespace detail;

    std::string flag = "\xF0\x9F\x87\xAE\xF0\x9F\x87\xB9";
    std::string text = "AB" + flag + "CD";
    const size_t input_clusters = grapheme_cluster_count(text);
    CHECK(input_clusters == 5);

    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = text;
    li.style.size = 24.0f;
    li.style.wrap = TextWrap::Character;
    li.box.enabled = true;
    li.box.size = {10.0f, 400.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res = TextLayoutEngine::layout(li);

    size_t total_line_clusters = 0;
    for (const auto& line : res.lines) {
        total_line_clusters += grapheme_cluster_count(line.text);
    }
    CHECK(total_line_clusters == input_clusters);
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. Inter-Token Tracking — Layout Engine
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: inter-token tracking — non-wrapping adds tracking between tokens") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "AB CD";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 10.0f;
    auto res10 = TextLayoutEngine::layout(li);
    float w10 = res10.size.x;

    CHECK(w10 == doctest::Approx(w0 + 40.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: inter-token tracking — three words have more gaps") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "A B C";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 10.0f;
    auto res10 = TextLayoutEngine::layout(li);
    float w10 = res10.size.x;

    CHECK(w10 == doctest::Approx(w0 + 40.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: inter-token tracking — single token has fewer gaps") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "ABC";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 10.0f;
    auto res10 = TextLayoutEngine::layout(li);
    float w10 = res10.size.x;

    CHECK(w10 == doctest::Approx(w0 + 20.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: inter-token tracking — single char with spaces has no gaps") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "A";
    li.style.size = 32.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 50.0f;
    auto res50 = TextLayoutEngine::layout(li);
    float w50 = res50.size.x;

    CHECK(w50 == doctest::Approx(w0).epsilon(0.02f));
}

TEST_CASE("TextQuality: inter-token tracking — word wrap preserves inter-token gaps") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "ABCDEF";
    li.style.size = 32.0f;
    li.style.wrap = TextWrap::Word;
    li.box.enabled = true;
    li.box.size = {100.0f, 200.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float total_width0 = 0.0f;
    for (const auto& line : res0.lines) total_width0 += line.width;

    li.style.tracking = 10.0f;
    auto res10 = TextLayoutEngine::layout(li);
    float total_width10 = 0.0f;
    for (const auto& line : res10.lines) total_width10 += line.width;

    CHECK(total_width10 == doctest::Approx(total_width0 + 50.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: inter-token tracking — multi-word wrap with tracking") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "AB CD EF";
    li.style.size = 24.0f;
    li.style.wrap = TextWrap::Word;
    li.box.enabled = true;
    li.box.size = {200.0f, 200.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    CHECK(res0.lines.size() == 1);
    float w0 = res0.lines[0].width;

    li.style.tracking = 8.0f;
    auto res8 = TextLayoutEngine::layout(li);
    CHECK(res8.lines.size() == 1);
    float w8 = res8.lines[0].width;

    CHECK(w8 == doctest::Approx(w0 + 56.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: inter-token tracking — ellipsis with tracking remeasures correctly") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "ABCD EFGH IJKL MNOP";
    li.style.size = 20.0f;
    li.style.max_lines = 1;
    li.style.overflow = TextOverflow::Ellipsis;
    li.box.enabled = true;
    li.box.size = {60.0f, 40.0f};
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res_tight = TextLayoutEngine::layout(li);
    CHECK_FALSE(res_tight.lines.empty());
    CHECK(res_tight.lines[0].width > 0.0f);
    CHECK(res_tight.lines[0].width <= 60.0f + 20.0f);

    li.style.tracking = 10.0f;
    auto res_tracking = TextLayoutEngine::layout(li);
    CHECK_FALSE(res_tracking.lines.empty());
    CHECK(res_tracking.lines[0].width > 0.0f);
    CHECK(res_tracking.lines[0].width <= 60.0f + 30.0f);
}

TEST_CASE("TextQuality: inter-token tracking — no tracking with zero value") {
    FontEngine engine;
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "A B C D E";
    li.style.size = 32.0f;
    li.style.tracking = 0.0f;
    li.font_engine = &engine;
    li.font_spec = inter_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    auto res0b = TextLayoutEngine::layout(li);

    CHECK(res0.size.x == doctest::Approx(res0b.size.x).epsilon(0.01f));
    CHECK(res0.lines.size() == res0b.lines.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. Typewriter Tracking — compute_typewriter_layout
// ═══════════════════════════════════════════════════════════════════════════

namespace ct = chronon3d::content::text;

TEST_CASE("TextQuality: typewriter tracking — width matches layout engine") {
    FontEngine engine;
    if (!require_font(engine)) return;

    const std::string text = "HELLO WORLD";
    const float size = 48.0f;
    const float tracking = 6.0f;
    const FontSpec spec = inter_bold_quality();

    TextLayoutInput li;
    li.text = text;
    li.style.size = size;
    li.style.tracking = tracking;
    li.style.wrap = TextWrap::None;
    li.font_engine = &engine;
    li.font_spec = spec;
    float layout_width = TextLayoutEngine::layout(li).size.x;

    auto tw = ct::compute_typewriter_layout(
        text, size, tracking, {2000.0f, 2000.0f}, 1.0f, spec);

    CHECK(tw.total_width == doctest::Approx(layout_width).epsilon(0.15f));
    CHECK(tw.chars.size() > 1);
}

TEST_CASE("TextQuality: typewriter tracking — zero tracking matches layout") {
    FontEngine engine;
    if (!require_font(engine)) return;

    const std::string text = "ABC";
    const float size = 32.0f;
    const float tracking = 0.0f;
    const FontSpec spec = inter_bold_quality();

    TextLayoutInput li;
    li.text = text;
    li.style.size = size;
    li.style.tracking = 0.0f;
    li.font_engine = &engine;
    li.font_spec = spec;
    float layout_width = TextLayoutEngine::layout(li).size.x;

    auto tw = ct::compute_typewriter_layout(
        text, size, tracking, {2000.0f, 2000.0f}, 1.0f, spec);

    CHECK(tw.total_width == doctest::Approx(layout_width).epsilon(0.02f));
    CHECK(tw.chars.size() == 3);
}

TEST_CASE("TextQuality: typewriter tracking — per-char advances sum to total") {
    FontEngine engine;
    if (!require_font(engine)) return;

    const std::string text = "ABCD";
    const float size = 48.0f;
    const float tracking = 8.0f;
    const FontSpec spec = inter_bold_quality();

    auto tw = ct::compute_typewriter_layout(
        text, size, tracking, {2000.0f, 2000.0f}, 1.0f, spec);

    REQUIRE(tw.chars.size() == 4);

    float sum_adv = 0.0f;
    for (const auto& cp : tw.chars) {
        sum_adv += cp.advance;
    }
    CHECK(sum_adv == doctest::Approx(tw.total_width).epsilon(0.01f));

    CHECK(tw.chars.back().advance > 0.0f);
}

TEST_CASE("TextQuality: typewriter tracking — with combining marks no double-count") {
    FontEngine engine;
    if (!require_font(engine)) return;

    const std::string text = "A" "e\xCC\x81" "B";
    const float size = 48.0f;
    const float tracking = 20.0f;
    const FontSpec spec = inter_bold_quality();

    auto tw = ct::compute_typewriter_layout(
        text, size, tracking, {2000.0f, 2000.0f}, 1.0f, spec);

    REQUIRE(tw.chars.size() == 3);

    TextLayoutInput li;
    li.text = text;
    li.style.size = size;
    li.style.tracking = 0.0f;
    li.font_engine = &engine;
    li.font_spec = spec;
    float raw_width = TextLayoutEngine::layout(li).size.x;

    float expected = raw_width + 40.0f;
    CHECK(tw.total_width == doctest::Approx(expected).epsilon(0.2f));

    MESSAGE("Char advances: ", tw.chars[0].advance, ", ",
            tw.chars[1].advance, ", ", tw.chars[2].advance);
}

TEST_CASE("TextQuality: typewriter tracking — with ZWJ emoji sequence") {
    FontEngine engine;
    if (!require_font(engine)) return;

    const std::string zwj_seq =
        "\xF0\x9F\x91\xA9"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA9";

    const std::string text = "A" + zwj_seq + "B";
    const float size = 64.0f;
    const float tracking = 10.0f;
    const FontSpec spec = inter_bold_quality();

    auto tw = ct::compute_typewriter_layout(
        text, size, tracking, {2000.0f, 2000.0f}, 1.0f, spec);

    CHECK(tw.total_width >= 0.0f);
    CHECK(tw.chars.size() >= 1);
    
    if (tw.chars.size() >= 2) {
        for (const auto& cp : tw.chars) {
            CHECK(cp.advance >= 0.0f);
        }
    }
}

TEST_CASE("TextQuality: typewriter tracking — different tracking values scale correctly") {
    FontEngine engine;
    if (!require_font(engine)) return;

    const std::string text = "TYPEWRITER";
    const float size = 40.0f;
    const FontSpec spec = inter_bold_quality();

    auto tw0 = ct::compute_typewriter_layout(
        text, size, 0.0f, {2000.0f, 2000.0f}, 1.0f, spec);
    size_t num_chars = tw0.chars.size();
    REQUIRE(num_chars > 1);

    float track5 = 5.0f * static_cast<float>(num_chars - 1);
    auto tw5 = ct::compute_typewriter_layout(
        text, size, 5.0f, {2000.0f, 2000.0f}, 1.0f, spec);
    CHECK(tw5.total_width == doctest::Approx(tw0.total_width + track5).epsilon(0.2f));

    float track15 = 15.0f * static_cast<float>(num_chars - 1);
    auto tw15 = ct::compute_typewriter_layout(
        text, size, 15.0f, {2000.0f, 2000.0f}, 1.0f, spec);
    CHECK(tw15.total_width == doctest::Approx(tw0.total_width + track15).epsilon(0.2f));
}
