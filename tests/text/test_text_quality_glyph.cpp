#include <optional>
#include "test_text_quality_helpers.hpp"
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
using namespace chronon3d;
using namespace test_text_quality;

// ═══════════════════════════════════════════════════════════════════════════
// 1. Glyph Placement — No Double-Advance
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: glyph placement — relative offsets used") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "ABC", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() >= 3);

    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& g = run->glyphs[i];
        INFO("Glyph ", i);
        CHECK(g.advance_x > 0.0f);
        CHECK(std::abs(g.x_offset) < g.advance_x * 2.0f);
        CHECK(std::abs(g.y_offset) < g.advance_x * 0.5f);
    }

    float sum_advances = 0.0f;
    for (const auto& g : run->glyphs) {
        sum_advances += g.advance_x;
    }
    CHECK(run->width == doctest::Approx(sum_advances).epsilon(0.02f));
}

TEST_CASE("    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value(); {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "AV", 32.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 2);

    const auto& g0 = run->glyphs[0];
    const auto& g1 = run->glyphs[1];

    float expected_x = g0.x_offset + g0.advance_x + g1.x_offset;
    CH    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();f));
}

TEST_CASE("TextQuality: glyph placement — single glyph has correct bbox") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto run = shape(engine, "M", 100.0f);
    REQUIRE(run.has_value());
    REQUIRE(run->glyphs.size() == 1);

    const auto& g = run->glyphs[0];
    CHECK(g.advance_x > 50.0f);
    CHECK(g.advance_x < 120.0f);
    CHECK(g.bbox_x0 < g.bbox_x1);
    CHECK(g.bbox_y1 < g.bbox_y0);
}

// ═════════════════════════════════════════════════════════════    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();r-Glyph
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: tracking 0/5/20 — increases width proportionally") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

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

    li.s    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();ngine::layout(li);
    float w20 = res20.size.x;

    CHECK(w5 == doctest::Approx(w0 + 10.0f).epsilon(0.2f));
    CHECK(w20 == doctest::Approx(w0 + 40.0f).epsilon(0.2f));
}

TEST_CASE("TextQuality: tracking — single character gets 0 tracking") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    TextLayoutInput li;
    li.text = "A";
    li.style.size = 32.0f;
      auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();r_bold_quality();

    auto res0 = TextLayoutEngine::layout(li);
    float w0 = res0.size.x;

    li.style.tracking = 20.0f;
    auto res20 = TextLayoutEngine::layout(li);
    float w20 = res20.size.x;

    CHECK(w20 == doctest::Approx(w0).epsilon(0.02f));
}

TEST_CASE("TextQuality: tracking — empty string has zero width") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
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

TEST_CASE("TextQuality: grapheme — e + combining acute accent is 1 cluster") {
    using namespace detail;

    const std::string e_acute = "e\xCC\x81";
    CHECK(utf8_length(e_acute) == 2);
    CHECK(grapheme_cluster_count(e_acute) == 1);
}

TEST_CASE("TextQuality: grapheme — ZWJ emoji family is 1 cluster") {
    using namespace detail;

    const std::string family =
        "\xF0\x9F\x91\xA9"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA9"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA7"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA6";

    CHECK(utf8_length(family) == 7);
    CHECK(grapheme_cluster_count(family) == 1);
}

TEST_CASE("TextQuality: grapheme — skin tone modifier stays attached") {
    using namespace detail;

    const std::string skin_tone =
        "\xF0\x9F\x91\x8D"
        "\xF0\x9F\x8F\xBB";

    CHECK(utf8_length(skin_tone) == 2);
    CHECK(grapheme_cluster_count(skin_tone) == 1);
}

TEST_CASE("TextQuality: grapheme — 🇮🇹 RI pair is 1 cluster") {
    using namespace detail;

    const std::string italy =
        "\xF0\x9F\x87\xAE"
        "\xF0\x9F\x87\xB9";

    CHECK(utf8_length(italy) == 2);
    CHECK(grapheme_cluster_count(italy) == 1);
}

TEST_CASE("TextQuality: grapheme — single RI followed by ASCII counts separately") {
    using namespace detail;

    const std::string ri_a =
        "\xF0\x9F\x87\xAE"
        "A";

    CHECK(grapheme_cluster_count(ri_a) == 2);
}

TEST_CASE("TextQuality: grapheme — byte offset at works for ZWJ emoji sequence") {
    using namespace detail;

    std::string zwj_seq =
        "\xF0\x9F\x91\xA9"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA9";

    CHECK(grapheme_cluster_count(zwj_seq) == 1);
    CHECK(grapheme_byte_offset_at(zwj_seq, 1) == 11);
}

TEST_CASE("TextQuality: grapheme — byte offset at works correctly") {
    using namespace detail;

    std::string text = "A" "e\xCC\x81" "B";

    CHECK(grapheme_cluster_count(text) == 3);
    CHECK(grapheme_byte_offset_at(text, 1) == 1);
    CHECK(grapheme_byte_offset_at(text, 2) == 4);
    CHECK(grapheme_byte_offset_at(text, 3) == 5);
    CHECK(grapheme_byte_offset_at(text, 10) == text.size());
}

TEST_CASE("TextQuality: grapheme — ASCII text count equals code point count") {
    using namespace detail;

    CHECK(grapheme_cluster_count("") == 0);
    CHECK(grapheme_cluster_count("A") == 1);
    CHECK(grapheme_cluster_count("ABC") == 3);
    CHECK(grapheme_cluster_count("Hello World") == 11);
}
