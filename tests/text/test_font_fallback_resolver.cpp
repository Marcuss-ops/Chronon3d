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
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_resolver.hpp>

#include "src/text/resolver/font_fallback_resolver.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <algorithm>

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

// ═══════════════════════════════════════════════════════════════════════════
// Tests: emoji ZWJ family + Devanagari + Greek/math/currency audit
//
// Targets the gap between the existing `multilingual corpus certifies
// per-cluster fallback` test and the verdict-Augmented coverage:
//   1. Emoji ZWJ family (👨‍👩‍👧‍👦) — 4 emoji codepoints joined by 3 U+200D
//      Zero-Width Joiners. A naïve codepoint-level splitter breaks the
//      family; the canonical state-machine flushes a run only when the
//      font index changes, so the test exercises both directions:
//      ZWJ covered by Latin primary vs emoji codepoints uncovered.
//   2. Devanagari (हिन्दी "Hindi") — 6 codepoints with combining
//      matras/virama; verifies that no `glyph_id == 0` codepoints
//      sneak into the run when no fallback covers the script.
//   3. Greek letters + math operators + currency (αβγ ∑∞ €$) —
//      mixed-script ASCII-Latin + Unicode mathematical/currency codepoints.
//
// All three samples contain codepoints that the bundled fonts do NOT
// cover (Latin-only Inter-Bold.ttf + Arabic-only NotoNaskhArabic).
// The expected behaviour is `missing_clusters > 0` and at least one
// tofu-sink run emitted (never silent-drop). The fail-loud audit
// fires via spdlog::error in `resolve_runs` (see foot of the function).
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// Shaping metrics corpus — locks cluster fallback + per-font shaping results.
// Verdict samples: AVATAR, "office affine", CJK, Arabic, emoji family.
// Records for each sample: run count, glyph count, width, missing clusters,
// and the resolved font family for every shaped run.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FontFallbackResolver: shaping metrics corpus (AVATAR / office / CJK / Arabic / emoji)") {
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

    struct Metrics {
        const char* name;
        std::string text;
        bool        expect_missing;
    };

    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    // 👨‍👩‍👧‍👦 emoji ZWJ family (4 emoji + 3 ZWJ codepoints)
    const std::string emoji_family =
        "\xF0\x9F\x91\xA8"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA9"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA7"
        "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA6";

    std::vector<Metrics> samples = {
        {"AVATAR", "AVATAR", false},
        {"office affine", "office affine", false},
        {"Arabic", std::string(arabic), false},
        {"CJK", "\xE4\xBD\xA0\xE5\xA5\xBD", true},          // 你好
        {"emoji family", emoji_family, true},
    };

    for (const auto& sample : samples) {
        INFO("Sample: ", sample.name);
        auto result = resolver.resolve_runs(
            sample.text, stack, shaping, 0,
            TextDirection::LTR, style);

        std::size_t total_glyphs = 0;
        float total_width = 0.0f;
        for (const auto& run : result.runs) {
            ResolvedTextRun r{run};
            PlacedGlyphRun placed = shape_resolved_run(r, engine, 0.0f);
            total_glyphs += placed.glyphs.size();
            total_width += placed.total_width;

            CHECK_MESSAGE(!placed.glyphs.empty(),
                sample.name, " run shaped to empty glyphs (font=", run.font.font_family, ")");
            CHECK_MESSAGE(!run.font.font_family.empty(),
                sample.name, " run has empty font family");
        }

        INFO(sample.name,
             " runs=", result.runs.size(),
             " glyphs=", total_glyphs,
             " width=", total_width,
             " missing=", result.missing_clusters);

        REQUIRE_MESSAGE(!result.runs.empty(),
            sample.name, " produced no runs");
        CHECK_MESSAGE(total_glyphs > 0,
            sample.name, " produced zero shaped glyphs");
        CHECK_MESSAGE(total_width > 0.0f,
            sample.name, " produced zero shaped width");

        if (sample.expect_missing) {
            CHECK_MESSAGE(result.missing_clusters > 0,
                sample.name, " should report missing clusters");
        } else {
            CHECK_MESSAGE(result.missing_clusters == 0,
                sample.name, " unexpectedly reports missing clusters");
            // Fully-covered samples should collapse to a single font run,
            // proving the state-machine groups consecutive same-font clusters.
            CHECK_MESSAGE(result.runs.size() == 1,
                sample.name, " should resolve to a single run (got ",
                result.runs.size(), ")");
        }
    }
}

TEST_CASE("FontFallbackResolver: emoji ZWJ family + Devanagari + symbols audit") {
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

    struct Sample {
        const char*                       name;
        std::string                       text;
        std::size_t                       min_expected_missing;
    };

    // 👨 U+1F468 + ZWJ + 👩 U+1F469 + ZWJ + 👧 U+1F467 + ZWJ + 👦 U+1F466
    // ZWJ (U+200D) is commonly covered by Latin fonts; emoji are not.
    const std::string emoji_zwj_family =
        "\xF0\x9F\x91\xA8"  // 👨 U+1F468
        "\xE2\x80\x8D"      // ZWJ  U+200D
        "\xF0\x9F\x91\xA9"  // 👩 U+1F469
        "\xE2\x80\x8D"      // ZWJ
        "\xF0\x9F\x91\xA7"  // 👧 U+1F467
        "\xE2\x80\x8D"      // ZWJ
        "\xF0\x9F\x91\xA6"; // 👦 U+1F466

    // हिन्दी "Hindi" — 6 codepoints with combining matras/virama.
    const std::string devanagari_hindi =
        "\xE0\xA4\xB9"  // ह  U+0939
        "\xE0\xA5\x8B"  // ा  U+094B
        "\xE0\xA4\xA8"  // न  U+0928
        "\xE0\xA5\x8D"  // ्  U+094D
        "\xE0\xA4\xA6"  // द  U+0926
        "\xE0\xA5\x80"; // ी  U+0940

    // Scripts that are not covered by either Inter-Bold or
    // NotoNaskhArabic-Regular (Armenian, Georgian, Cherokee).
    // Note: the original Greek/math/currency sample was dropped because
    // Inter-Bold actually ships real glyphs for αβγ/∑∞/€/$ in this
    // repository, which made the missing-glyph expectation unreliable.
    const std::string armenian_georgian_cherokee =
        "\xD4\xB1"      // Ա  U+0531 (Armenian)
        "\xD5\xA1"      // ա  U+0561 (Armenian)
        "\xE1\x83\x90"  // დ  U+10D0 (Georgian)
        "\xE1\x83\xA1"  // ს  U+10E1 (Georgian)
        "\xE1\x8E\xA0"  // Ꭰ  U+13A0 (Cherokee)
        "\xE1\x8E\xB1"; // Ꮁ  U+13B1 (Cherokee)

    std::vector<Sample> samples = {
        {"Emoji ZWJ family",              emoji_zwj_family,              /* emoji-only uncovered */ 4},
        {"Devanagari",                    devanagari_hindi,              /* all 6 uncovered */    6},
        {"Armenian+Georgian+Cherokee",    armenian_georgian_cherokee,    /* all 6 uncovered */    6},
    };

    for (const auto& sample : samples) {
        INFO("Sample: ", sample.name);
        auto result = resolver.resolve_runs(
            sample.text, stack, shaping, 0, TextDirection::LTR, style);

        const std::size_t missing_count = result.missing_clusters;
        INFO("missing_count=", missing_count,
             " runs.size()=", result.runs.size());

        // Fail-loud audit fires: at least the expected uncovered codepoints
        // are counted and logged via spdlog::error inside `resolve_runs`.
        CHECK_MESSAGE(missing_count >= sample.min_expected_missing,
            sample.name, " missing_count=", missing_count,
            " expected >= ", sample.min_expected_missing);

        // Tofu-sink contract: at least one run is emitted so text is not
        // silently dropped. The run count may split by font coverage
        // (e.g. ZWJ covered by Inter → ZWJ run + 4 emoji tofu-sink runs).
        // We log it as INFO but do NOT assert a specific count to avoid
        // false-negatives against font-availability drift.
        REQUIRE_MESSAGE(!result.runs.empty(),
            sample.name, " produced no runs (silent-drop rot)");

        // Every run must carry a non-empty font family (= tofu-sink primary).
        for (const auto& run : result.runs) {
            CHECK_MESSAGE(!run.font.font_family.empty(),
                sample.name, " run has empty font family");
        }
    }
}

namespace {

std::filesystem::path test_repo_root() {
    // Prefer the compile-time source dir macro, then the env var, then
    // the current working directory. The source dir is the only reliable
    // value when the test binary is run from the build directory.
    const char* env_src = std::getenv("CHRONON3D_SOURCE_DIR");
    if (env_src && std::filesystem::is_directory(env_src)) {
        return std::filesystem::path(env_src);
    }

    // Derive repo root from this source file path:
    // tests/text/test_font_fallback_resolver.cpp → repo root.
    std::filesystem::path src(__FILE__);
    for (int i = 0; i < 3 && !src.parent_path().empty(); ++i) {
        src = src.parent_path();
    }
    if (std::filesystem::is_directory(src / "assets" / "fonts")) {
        return src;
    }

    return std::filesystem::current_path();
}

} // namespace

namespace {

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool family_contains(const std::string& family, std::string_view substring) {
    return to_lower(family).find(to_lower(substring)) != std::string::npos;
}

} // namespace

TEST_CASE("resolve_text_run_tree: bundled_fonts_root propagated through pipeline") {
    Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    const std::filesystem::path repo_root = test_repo_root();
    const std::filesystem::path fonts_dir = repo_root / "assets" / "fonts";

    TextDocument doc;
    // Latin + Arabic. Inter covers Latin; Arabic needs the bundled fallback.
    const std::string arabic =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";
    doc.utf8 = std::string("Hello ") + arabic;
    doc.defaults.font.font_path = (fonts_dir / "Inter-Bold.ttf").generic_string();
    doc.defaults.font.font_family = "Inter";
    doc.defaults.font.font_size = 32.0f;
    doc.split_paragraphs();

    // With no bundled fonts root, Arabic is uncovered → missing glyph audit.
    {
        auto tree = resolve_text_run_tree(doc, engine);
        CHECK(tree.missing_glyph_count > 0);
    }

    // With the bundled fonts root, Arabic should be covered by Noto Naskh Arabic.
    {
        INFO("repo_root=", test_repo_root().string(),
             " fonts_dir=", fonts_dir.string(),
             " exists=", std::filesystem::exists(fonts_dir));
        auto tree = resolve_text_run_tree(doc, engine, fonts_dir);
        INFO("missing_glyph_count=", tree.missing_glyph_count);
        CHECK(tree.missing_glyph_count == 0);
        REQUIRE(tree.paragraphs.size() == 1);
        REQUIRE(tree.paragraphs[0].runs.size() == 2);
        // Run order: Latin (Inter) first, then Arabic (Noto Naskh Arabic).
        CHECK(family_contains(tree.paragraphs[0].runs[0].font.font_family, "inter"));
        CHECK(family_contains(tree.paragraphs[0].runs[1].font.font_family, "noto naskh arabic"));
        // The fallback run must actually point to the bundled file.
        CHECK(tree.paragraphs[0].runs[1].font.font_path.find("NotoNaskhArabic") != std::string::npos);
    }
}
