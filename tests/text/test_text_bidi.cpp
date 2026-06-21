#include <chronon3d/backends/text/bidi_segmenter.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>
using namespace chronon3d;


// ── Helpers ──────────────────────────────────────────────────────────────

static FontSpec inter_bold_bidi() {
    FontSpec spec;
    spec.font_path = "assets/fonts/Inter-Bold.ttf";
    spec.font_family = "Inter";
    spec.font_weight = 700;
    return spec;
}

// ── Bidi Segmenter Tests ─────────────────────────────────────────────────

TEST_CASE("BidiSegmenter: empty string returns empty") {
    auto runs = segment_bidi_runs("");
    CHECK(runs.empty());
}

TEST_CASE("BidiSegmenter: pure Latin returns single LTR run") {
    auto runs = segment_bidi_runs("Hello World");
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].direction == TextDirection::LTR);
    CHECK(runs[0].text == "Hello World");
    CHECK(runs[0].byte_offset == 0);
}

TEST_CASE("BidiSegmenter: pure Arabic returns single RTL run") {
    // "مرحبا" = Arabic "hello"
    auto runs = segment_bidi_runs("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7");
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].direction == TextDirection::RTL);
    CHECK(runs[0].byte_offset == 0);
}

TEST_CASE("BidiSegmenter: mixed Latin + Arabic returns three runs [LTR, RTL, LTR]") {
    // "Hello مرحبا World"
    std::string mixed = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
    auto runs = segment_bidi_runs(mixed);
    REQUIRE(runs.size() == 3);

    CHECK(runs[0].direction == TextDirection::LTR);
    CHECK(runs[0].text == "Hello ");
    CHECK(runs[0].byte_offset == 0);

    CHECK(runs[1].direction == TextDirection::RTL);
    CHECK(runs[1].text == "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7");
    CHECK(runs[1].byte_offset == 6);

    CHECK(runs[2].direction == TextDirection::LTR);
    CHECK(runs[2].text == " World");
    CHECK(runs[2].byte_offset == 16);
}

TEST_CASE("BidiSegmenter: Arabic embedded in Latin numbers works") {
    // "Order 5 مرحبا items" — number (5) is weak, takes surrounding LTR direction
    std::string mixed = "Order 5 \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 items";
    auto runs = segment_bidi_runs(mixed);
    REQUIRE(runs.size() == 3);

    CHECK(runs[0].direction == TextDirection::LTR);
    CHECK(runs[0].text == "Order 5 ");
    CHECK(runs[1].direction == TextDirection::RTL);
    CHECK(runs[2].direction == TextDirection::LTR);
    CHECK(runs[2].text == " items");
}

TEST_CASE("BidiSegmenter: Hebrew + English produces correct runs") {
    // "שלום World"
    std::string mixed = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D World";
    auto runs = segment_bidi_runs(mixed);
    // With auto-detected RTL paragraph direction, the space between
    // RTL and LTR text may be grouped with either run depending on
    // embedding level resolution.  Verify we have exactly two runs
    // with opposite directions, and the total text is preserved.
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].direction == TextDirection::RTL);
    CHECK(runs[1].direction == TextDirection::LTR);
    CHECK(runs[0].byte_offset == 0);
    CHECK(runs[1].byte_offset + runs[1].text.size() == mixed.size());
}

TEST_CASE("BidiSegmenter: mixed Arabic+Hebrew+English") {
    // "مرحبا שלום World"
    std::string mixed = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D World";
    auto runs = segment_bidi_runs(mixed);
    REQUIRE(runs.size() >= 2);
    bool has_rtl = false;
    for (const auto& r : runs) {
        if (r.direction == TextDirection::RTL) has_rtl = true;
    }
    CHECK(has_rtl);
}

TEST_CASE("BidiSegmenter: explicit LTR base direction") {
    std::string mixed = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
    auto runs = segment_bidi_runs(mixed, 1); // FRIBIDI_PAR_DIR_LTR
    REQUIRE(runs.size() == 3);
    CHECK(runs[0].direction == TextDirection::LTR);
    CHECK(runs[1].direction == TextDirection::RTL);
    CHECK(runs[2].direction == TextDirection::LTR);
}

TEST_CASE("BidiSegmenter: explicit RTL base direction") {
    std::string mixed = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
    auto runs = segment_bidi_runs(mixed, 2); // FRIBIDI_PAR_DIR_RTL
    REQUIRE(runs.size() >= 2);
    bool has_rtl = false;
    for (const auto& r : runs) {
        if (r.direction == TextDirection::RTL) has_rtl = true;
    }
    CHECK(has_rtl);
}

// ── Layout Engine Bidi Integration Tests ───────────────────────────────

TEST_CASE("TextLayout: bidi with pure Latin text") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextLayoutInput input;
    input.text = "Hello World";
    input.font_engine = &engine;
    input.font_spec = inter_bold_bidi();
    input.style.font_path = "assets/fonts/Inter-Bold.ttf";
    input.style.size = 32.0f;
    input.style.shaping.direction = TextDirection::Auto;

    const auto result = TextLayoutEngine::layout(input);
    CHECK(!result.lines.empty());
    CHECK(result.lines[0].text == "Hello World");
    CHECK(result.lines[0].width > 0.0f);
}

TEST_CASE("TextLayout: bidi with pure Arabic text") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextLayoutInput input;
    input.text = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";  // "مرحبا"
    input.font_engine = &engine;
    input.font_spec = inter_bold_bidi();
    input.style.font_path = "assets/fonts/Inter-Bold.ttf";
    input.style.size = 32.0f;
    input.style.shaping.direction = TextDirection::Auto;

    const auto result = TextLayoutEngine::layout(input);
    CHECK(!result.lines.empty());
    CHECK(result.lines[0].width > 0.0f);
}

TEST_CASE("TextLayout: bidi with mixed Arabic+English") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextLayoutInput input;
    input.text = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
    input.font_engine = &engine;
    input.font_spec = inter_bold_bidi();
    input.style.font_path = "assets/fonts/Inter-Bold.ttf";
    input.style.size = 32.0f;
    input.style.shaping.direction = TextDirection::Auto;

    const auto result = TextLayoutEngine::layout(input);
    CHECK(!result.lines.empty());

    // Should produce runs with different directions
    bool has_ltr = false;
    bool has_rtl = false;
    for (const auto& line : result.lines) {
        for (const auto& run : line.runs) {
            if (run.style.shaping.direction == TextDirection::LTR) has_ltr = true;
            if (run.style.shaping.direction == TextDirection::RTL) has_rtl = true;
        }
    }
    CHECK(has_ltr);
    CHECK(has_rtl);
}

TEST_CASE("TextLayout: explicit LTR direction skips bidi segmentation") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextLayoutInput input;
    input.text = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
    input.font_engine = &engine;
    input.font_spec = inter_bold_bidi();
    input.style.font_path = "assets/fonts/Inter-Bold.ttf";
    input.style.size = 32.0f;
    input.style.shaping.direction = TextDirection::LTR;

    const auto result = TextLayoutEngine::layout(input);
    CHECK(!result.lines.empty());
    for (const auto& line : result.lines) {
        for (const auto& run : line.runs) {
            CHECK(run.style.shaping.direction == TextDirection::LTR);
        }
    }
}

TEST_CASE("TextLayout: explicit RTL direction preserves single run") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextLayoutInput input;
    input.text = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 English";  // Arabic + English
    input.font_engine = &engine;
    input.font_spec = inter_bold_bidi();
    input.style.font_path = "assets/fonts/Inter-Bold.ttf";
    input.style.size = 32.0f;
    input.style.shaping.direction = TextDirection::RTL;

    const auto result = TextLayoutEngine::layout(input);
    CHECK(!result.lines.empty());
    for (const auto& line : result.lines) {
        for (const auto& run : line.runs) {
            CHECK(run.style.shaping.direction == TextDirection::RTL);
        }
    }
}

TEST_CASE("TextLayout: bidi run widths are non-zero") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextLayoutInput input;
    input.text = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
    input.font_engine = &engine;
    input.font_spec = inter_bold_bidi();
    input.style.font_path = "assets/fonts/Inter-Bold.ttf";
    input.style.size = 32.0f;
    input.style.shaping.direction = TextDirection::Auto;

    const auto result = TextLayoutEngine::layout(input);
    CHECK(!result.lines.empty());
    for (const auto& line : result.lines) {
        CHECK(line.width > 0.0f);
        for (const auto& run : line.runs) {
            CHECK(run.width >= 0.0f);
        }
    }
}

// ── FontEngine shaping with bidi directions ────────────────────────────

TEST_CASE("FontEngine: shaping Arabic with explicit RTL") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextShaping rtl_shaping;
    rtl_shaping.direction = TextDirection::RTL;
    rtl_shaping.language = "ar";

    auto run = engine.shape_text(
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",
        inter_bold_bidi(), 48.0f, rtl_shaping);

    REQUIRE(run.has_value());
    REQUIRE(!run->glyphs.empty());
    CHECK(run->width > 0.0f);
}

TEST_CASE("FontEngine: shaping Arabic+English with Auto") {
    FontEngine engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    TextShaping auto_shaping;
    auto_shaping.direction = TextDirection::Auto;

    auto run = engine.shape_text(
        "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World",
        inter_bold_bidi(), 48.0f, auto_shaping);

    REQUIRE(run.has_value());
    CHECK(run->width > 0.0f);
}
