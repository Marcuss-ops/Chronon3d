// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_font_fallback_resolver.cpp
//
// Unit / integration tests for the canonical cluster-level font fallback
// service (src/text/resolver/font_fallback_resolver).
//
// Covers:
//   * Single-font run when the primary covers all codepoints.
//   * Splitting a mixed-script string into per-font runs.
//   * Fail-loud missing-glyph audit when no font in the stack covers a
//     codepoint.
//   * Shaping of the resolved runs through the existing FontEngine path.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_resolver.hpp>

#include "src/text/resolver/font_fallback_resolver.hpp"

#include <cstddef>
#include <string>
#include <string_view>

using namespace chronon3d;
using namespace chronon3d::text::resolver;

namespace {

FontSpec make_spec(const char* path, const char* family, int weight, float size) {
    FontSpec spec;
    spec.font_path   = path;
    spec.font_family = family;
    spec.font_weight = weight;
    spec.font_size   = size;
    return spec;
}

} // namespace

TEST_CASE("FontFallbackResolver: single font covers ASCII") {
    Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    FontStack stack;
    stack.push_back(make_spec("assets/fonts/Inter-Bold.ttf", "Inter", 700, 32.0f));

    TextShaping shaping;
    ParagraphStyle style;
    FontFallbackResolver resolver{engine};
    auto result = resolver.resolve_runs(
        "Hello", stack, shaping, 0, TextDirection::LTR, style);

    CHECK(result.runs.size() == 1);
    CHECK(result.missing_clusters == 0);
    if (!result.runs.empty()) {
        CHECK(result.runs[0].text == "Hello");
        CHECK(result.runs[0].font.font_family == "Inter");
    }
}

TEST_CASE("FontFallbackResolver: Latin + Arabic splits by coverage") {
    Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    // Primary font lacks Arabic; fallback font covers it.
    FontStack stack;
    stack.push_back(make_spec("assets/fonts/Inter-Bold.ttf", "Inter", 700, 32.0f));
    stack.push_back(make_spec("assets/fonts/NotoNaskhArabic-Regular.ttf", "Noto Naskh Arabic", 400, 32.0f));

    TextShaping shaping;
    ParagraphStyle style;
    FontFallbackResolver resolver{engine};

    // "Hello " (Latin) + "مرحبا" (Arabic).
    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";
    const std::string text = "Hello " + arabic;

    auto result = resolver.resolve_runs(
        text, stack, shaping, 10, TextDirection::RTL, style);

    CHECK(result.missing_clusters == 0);
    REQUIRE(result.runs.size() == 2);

    CHECK(result.runs[0].text == "Hello ");
    CHECK(result.runs[0].font.font_family == "Inter");
    CHECK(result.runs[0].direction == TextDirection::RTL);
    CHECK(result.runs[0].byte_offset == 10);

    CHECK(result.runs[1].text == arabic);
    CHECK(result.runs[1].font.font_family == "Noto Naskh Arabic");
    CHECK(result.runs[1].direction == TextDirection::RTL);
    CHECK(result.runs[1].byte_offset == 16);
}

TEST_CASE("FontFallbackResolver: missing glyph is logged and counted") {
    Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    // Stack with only the Latin font — Arabic codepoints are uncovered.
    FontStack stack;
    stack.push_back(make_spec("assets/fonts/Inter-Bold.ttf", "Inter", 700, 32.0f));

    TextShaping shaping;
    ParagraphStyle style;
    FontFallbackResolver resolver{engine};

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    auto result = resolver.resolve_runs(
        arabic, stack, shaping, 0, TextDirection::RTL, style);

    CHECK(result.missing_clusters == 5); // 5 Arabic codepoints
    REQUIRE(!result.runs.empty());
    // Missing glyphs fall back to the primary font (tofu sink) so text is
    // not silently dropped.
    CHECK(result.runs[0].font.font_family == "Inter");
}

TEST_CASE("FontFallbackResolver: resolved runs can be shaped") {
    Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    FontStack stack;
    stack.push_back(make_spec("assets/fonts/Inter-Bold.ttf", "Inter", 700, 32.0f));
    stack.push_back(make_spec("assets/fonts/NotoNaskhArabic-Regular.ttf", "Noto Naskh Arabic", 400, 32.0f));

    TextShaping shaping;
    ParagraphStyle style;
    FontFallbackResolver resolver{engine};

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";
    const std::string text = "Hi " + arabic;

    auto result = resolver.resolve_runs(
        text, stack, shaping, 0, TextDirection::RTL, style);

    REQUIRE(result.runs.size() == 2);

    for (const auto& run : result.runs) {
        ResolvedTextRun r{run};
        PlacedGlyphRun placed = shape_resolved_run(r, engine, 0.0f);
        CHECK(!placed.glyphs.empty());
    }
}

TEST_CASE("FontFallbackResolver: multilingual corpus certifies per-cluster fallback") {
    Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    FontStack stack;
    stack.push_back(make_spec("assets/fonts/Inter-Bold.ttf", "Inter", 700, 32.0f));
    stack.push_back(make_spec("assets/fonts/NotoNaskhArabic-Regular.ttf", "Noto Naskh Arabic", 400, 32.0f));

    TextShaping shaping;
    ParagraphStyle style;
    FontFallbackResolver resolver{engine};

    // Corpus subset that can be certified with the fonts available in the
    // repository.  Each entry is a distinct typographic category.
    struct Sample {
        const char* name;
        std::string text;
        bool expect_missing;
    };

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    // The repository only bundles Latin and Arabic fonts. Scripts that we
    // do not have a font for are deliberately included to certify the
    // fail-loud missing-glyph audit.
    std::vector<Sample> samples = {
        {"ASCII / Latin",          "AVATAR",                               false},
        {"Latin with accents",     "Caf\xC3\xA9 d\xC3\xA9j\xC3\xA0 vu",     false},
        {"Mixed Latin + Arabic",   std::string("Hello ") + arabic,        false},
        {"Arabic only (fallback)", arabic,                                   false},
        {"Hebrew (uncovered)",     "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",      true},
        {"CJK (uncovered)",        "\xE4\xBD\xA0\xE5\xA5\xBD",             true},
        {"Emoji (uncovered)",      "\xF0\x9F\x8D\x8E",                     true},
    };

    for (const auto& sample : samples) {
        INFO("Sample: ", sample.name);
        auto result = resolver.resolve_runs(
            sample.text, stack, shaping, 0,
            TextDirection::LTR, style);

        REQUIRE_MESSAGE(!result.runs.empty(), sample.name, " produced no runs");

        if (sample.expect_missing) {
            CHECK_MESSAGE(result.missing_clusters > 0, sample.name, " should report missing glyphs");
        } else {
            CHECK_MESSAGE(result.missing_clusters == 0, sample.name, " has missing glyphs");
        }

        // Every run must shape to at least one glyph.
        for (const auto& run : result.runs) {
            ResolvedTextRun r{run};
            PlacedGlyphRun placed = shape_resolved_run(r, engine, 0.0f);
            CHECK_MESSAGE(!placed.glyphs.empty(), sample.name, " run shaped to empty glyphs");
        }
    }
}
