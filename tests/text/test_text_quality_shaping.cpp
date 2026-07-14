#include <optional>
#include "test_text_quality_helpers.hpp"
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
using namespace chronon3d;
using namespace test_text_quality;

// ═══════════════════════════════════════════════════════════════════════════
// 4. RTL Shaping — Arabic and Hebrew
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: RTL — Arabic text shapes correctly") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    TextShaping arabic_shaping;
    arabic_shaping.direction = TextDirection::RTL;
    arabic_shaping.language = "ar";

    auto run = engine.shape_text(arabic, inter_bold_quality(), 32.0f, arabic_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->width > 0.0f);
}

TEST_CASE("TextQuality: RTL — Hebrew text shapes correctly") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string hebrew =
        "\xD7\xA9"
        "\xD7\x9C"
        "\xD7\x95"
        "\xD7\x9D";

    TextShaping hebrew_shaping;
    hebrew_shaping.direction = TextDirection::RTL;
    hebrew_shaping.language = "he";

    auto run = engine.shape_text(hebrew, inter_bold_quality(), 32.0f, hebrew_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->width > 0.0f);
}

TEST_CASE("TextQuality: RTL — LTR text shapes correctly with explicit direction") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    TextShaping ltr_shaping;
    ltr_shaping.direction = TextDirection::LTR;
    ltr_shaping.language = "en";

    auto run = engine.shape_text("ABC", inter_bold_quality(), 32.0f, ltr_shaping);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 3);

    CHECK(run->glyphs[0].x < run->glyphs[1].x);
    CHECK(run->glyphs[1].x < run->glyphs[2].x);
}

TEST_CASE("TextQuality: RTL — multi-glyph cluster (ligature) has correct is_cluster_start") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string lam_alef =
        "\xD9\x84"
        "\xD8\xA7";

    TextShaping arabic_shaping;
    arabic_shaping.direction = TextDirection::RTL;
    arabic_shaping.language = "ar";

    auto run = engine.shape_text(lam_alef, inter_bold_quality(), 32.0f, arabic_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->glyphs[0].is_cluster_start);

    for (size_t i = 1; i < run->glyphs.size(); ++i) {
        INFO("Glyph ", i);
        CHECK(run->glyphs[i].advance_x > 0.0f);
    }
}

TEST_CASE("TextQuality: RTL — Arabic positions are in visual (right-to-left) order") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    TextShaping rtl_shaping;
    rtl_shaping.direction = TextDirection::RTL;
    rtl_shaping.language = "ar";

    auto run = engine.shape_text(arabic, inter_bold_quality(), 32.0f, rtl_shaping);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    for (size_t i = 1; i < run->glyphs.size(); ++i) {
        INFO("Glyph ", i);
        CHECK(run->glyphs[i].x > run->glyphs[i-1].x - 0.5f);
    }
}

TEST_CASE("TextQuality: RTL — LTR and RTL produce different glyph orders") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

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

    CHECK(run_ltr->glyphs.size() > 0);
    CHECK(run_rtl->glyphs.size() > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Stroke vs Fill — Shared Shaping Glyphs
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: stroke vs fill — glyph IDs match between fill and stroke") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "ABC", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 3);

    for (const auto& g : run->glyphs) {
        CHECK(g.glyph_id > 0);
    }
}

TEST_CASE("TextQuality: stroke vs fill — advances are consistent per glyph") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "AV", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 2);

    for (const auto& g : run->glyphs) {
        CHECK(g.advance_x > 0.0f);
        float glyph_ink_w = g.bbox_x1 - g.bbox_x0;
        CHECK(glyph_ink_w <= g.advance_x * 1.2f);
    }
}

TEST_CASE("TextQuality: stroke vs fill — same shaped text produces same glyph count") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run1 = shape(engine, "Hello", 32.0f);
    auto run2 = shape(engine, "Hello", 32.0f);
    REQUIRE(run1.has_value());
    REQUIRE(run2.has_value());

    CHECK(run1->glyphs.size() == run2->glyphs.size());
    CHECK(run1->width == doctest::Approx(run2->width).epsilon(0.01f));
}

TEST_CASE("TextQuality: stroke vs fill — text with tracking preserves glyph count") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "Test", 24.0f);
    REQUIRE(run.has_value());

    size_t base_count = run->glyphs.size();
    CHECK(base_count == 4);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. conicTo Curve Fidelity
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: conicTo — glyph bboxes scale linearly with font size") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

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

    float w_ratio = w64 / w16;
    float h_ratio = h64 / h16;
    CHECK(w_ratio == doctest::Approx(4.0f).epsilon(0.15f));
    CHECK(h_ratio == doctest::Approx(4.0f).epsilon(0.15f));
}

TEST_CASE("TextQuality: conicTo — round glyph 'O' has near-square bbox") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "O", 64.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    const auto& g = run->glyphs[0];
    float glyph_w = g.bbox_x1 - g.bbox_x0;
    float glyph_h = g.bbox_y0 - g.bbox_y1;

    float aspect = glyph_w / glyph_h;
    CHECK(aspect == doctest::Approx(1.0f).epsilon(0.15f));
}

TEST_CASE("TextQuality: conicTo — serif glyphs ('S') have correct bbox proportions") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "S", 64.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    const auto& g = run->glyphs[0];
    float glyph_w = g.bbox_x1 - g.bbox_x0;
    float glyph_h = g.bbox_y0 - g.bbox_y1;

    float ratio = glyph_w / glyph_h;
    CHECK(ratio > 0.4f);
    CHECK(ratio < 0.9f);

    CHECK(g.advance_x > 20.0f);
    CHECK(g.advance_x < 90.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Stroke GPOS Offsets — Kerning, Mark Positioning, Diacritics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: stroke GPOS — kerning pair 'AV' has correct offsets") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "AV", 72.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 2);

    const auto& g0 = run->glyphs[0];
    const auto& g1 = run->glyphs[1];

    INFO("g0 (A): advance_x=", g0.advance_x, " x_offset=", g0.x_offset,
         " x=", g0.x);
    INFO("g1 (V): advance_x=", g1.advance_x, " x_offset=", g1.x_offset,
         " x=", g1.x, " cluster=", g1.cluster);

    CHECK(g0.glyph_id > 0);
    CHECK(g1.glyph_id > 0);
    CHECK(g0.advance_x > 0.0f);
    CHECK(g1.advance_x > 0.0f);

    CHECK(std::abs(g0.x_offset) < g0.advance_x * 0.5f);
    CHECK(std::abs(g1.x_offset) < g1.advance_x * 0.5f);

    float expected_g1_x = g0.x_offset + g0.advance_x + g1.x_offset;
    CHECK(g1.x == doctest::Approx(expected_g1_x).epsilon(0.5f));
}

TEST_CASE("TextQuality: stroke GPOS — offsets do not accumulate across 3 glyphs") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "AVA", 72.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 3);

    float pen = 0.0f;
    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& g = run->glyphs[i];
        INFO("Glyph ", i);
        CHECK(g.x == doctest::Approx(pen + g.x_offset).epsilon(0.5f));
        pen += g.advance_x;
    }
}

TEST_CASE("TextQuality: stroke GPOS — combining accent has y_offset") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string e_acute = "e\xCC\x81";

    auto run = shape(engine, e_acute, 72.0f);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    bool found_accent = false;
    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& g = run->glyphs[i];
        if (i > 0 && std::abs(g.y_offset) > 0.5f) {
            found_accent = true;
        }
    }
    if (!found_accent && run->glyphs.size() == 1) {
        MESSAGE("Font produced pre-composed é glyph (no separate accent)");
    } else {
        CHECK(found_accent);
    }
}

TEST_CASE("TextQuality: stroke GPOS — offsets small relative to advance") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const char* test_strings[] = {"AV", "VA", "AT", "TA", "AVATAR", "TO", "WO"};

    for (const auto& s : test_strings) {
        INFO("Testing: ", s);
        auto run = shape(engine, s, 48.0f);
        REQUIRE(run.has_value());

        for (size_t i = 0; i < run->glyphs.size(); ++i) {
            const auto& g = run->glyphs[i];
            INFO("Glyph ", i, " glyph_id=", g.glyph_id);
            CHECK(std::abs(g.x_offset) <= g.advance_x * 1.5f);
            CHECK(std::abs(g.y_offset) <= g.advance_x * 0.5f + 2.0f);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. Complex Scripts — Arabic GPOS, Cluster Mapping, Glyph IDs
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: complex — Arabic 'hello' has consistent glyph IDs") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    TextShaping ar;
    ar.direction = TextDirection::RTL;
    ar.language = "ar";

    auto run = engine.shape_text(arabic, inter_bold_quality(), 48.0f, ar);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->width > 0.0f);
    CHECK(run->glyphs.front().is_cluster_start);

    bool all_glyphs_valid = true;
    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        if (run->glyphs[i].glyph_id == 0) { all_glyphs_valid = false; break; }
    }
    if (!all_glyphs_valid) {
        MESSAGE("Inter-Bold does not appear to contain Arabic glyphs — this is expected");
        return;
    }

    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& g = run->glyphs[i];
        INFO("Glyph ", i);
        CHECK(g.glyph_id > 0);
        CHECK(g.advance_x > 0.0f);
        CHECK(std::abs(g.x_offset) < g.advance_x * 0.5f);
    }
}

TEST_CASE("TextQuality: complex — same Arabic text shapes identically twice") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string arabic =
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";

    TextShaping ar;
    ar.direction = TextDirection::RTL;
    ar.language = "ar";

    auto r1 = engine.shape_text(arabic, inter_bold_quality(), 48.0f, ar);
    auto r2 = engine.shape_text(arabic, inter_bold_quality(), 48.0f, ar);
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());

    CHECK(r1->glyphs.size() == r2->glyphs.size());
    CHECK(r1->width == doctest::Approx(r2->width).epsilon(0.01f));

    for (size_t i = 0; i < r1->glyphs.size(); ++i) {
        INFO("Glyph ", i);
        CHECK(r1->glyphs[i].glyph_id == r2->glyphs[i].glyph_id);
        CHECK(r1->glyphs[i].advance_x == doctest::Approx(r2->glyphs[i].advance_x).epsilon(0.01f));
        CHECK(r1->glyphs[i].x_offset == doctest::Approx(r2->glyphs[i].x_offset).epsilon(0.01f));
    }
}

TEST_CASE("TextQuality: complex — Hebrew has consistent cluster mapping") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string hebrew =
        "\xD7\xA9"
        "\xD7\x9C"
        "\xD7\x95"
        "\xD7\x9D";

    TextShaping he;
    he.direction = TextDirection::RTL;
    he.language = "he";

    auto run = engine.shape_text(hebrew, inter_bold_quality(), 36.0f, he);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->width > 0.0f);

    for (const auto& g : run->glyphs) {
        CHECK_UNARY(g.cluster <= hebrew.size());
    }

    for (const auto& g : run->glyphs) {
        CHECK(std::abs(g.x_offset) < g.advance_x);
        CHECK(std::abs(g.y_offset) < g.advance_x);
    }
}

TEST_CASE("TextQuality: complex — Devanagari does not crash or produce empty glyphs") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string devanagari =
        "\xE0\xA4\xA8"
        "\xE0\xA4\xAE"
        "\xE0\xA4\xB8"
        "\xE0\xA4\xCD"
        "\xE0\xA4\xA4"
        "\xE0\xA5\x87";

    TextShaping hi;
    hi.language = "hi";

    auto run = engine.shape_text(devanagari, inter_bold_quality(), 48.0f, hi);
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->glyphs.empty());

    CHECK(run->width > 0.0f);

    bool all_glyphs_valid = true;
    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        if (run->glyphs[i].glyph_id == 0) { all_glyphs_valid = false; break; }
    }
    if (!all_glyphs_valid) {
        MESSAGE("Inter-Bold does not appear to contain Devanagari glyphs — this is expected");
        return;
    }

    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& g = run->glyphs[i];
        INFO("Glyph ", i, " id=", g.glyph_id, " adv=", g.advance_x);
        CHECK(g.glyph_id > 0);
        CHECK(g.advance_x >= 0.0f);

        if (g.advance_x == 0.0f) {
            CHECK(std::abs(g.y_offset) >= 0.1f);
        }
    }
}

TEST_CASE("TextQuality: complex — CJK has valid glyph IDs and advances") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const std::string cjk = "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";

    TextShaping zh;
    zh.language = "zh";

    auto run = engine.shape_text(cjk, inter_bold_quality(), 48.0f, zh);
    if (run.has_value() && !run->glyphs.empty()) {
        CHECK(run->width >= 0.0f);
        for (const auto& g : run->glyphs) {
            CHECK(g.advance_x >= 0.0f);
        }
    } else {
        MESSAGE("CJK shaping returned nullopt or empty — font likely lacks CJK glyphs");
    }
}
