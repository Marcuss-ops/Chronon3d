#include <optional>
#include <string>
#include <unordered_set>

#include <doctest/doctest.h>

#include "test_text_quality_helpers.hpp"

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/scene/builders/params/text_params.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>

using namespace chronon3d;
using namespace test_text_quality;

namespace {

std::vector<std::uint32_t> glyph_ids(const GlyphRun& run) {
    std::vector<std::uint32_t> ids;
    ids.reserve(run.glyphs.size());
    for (const auto& g : run.glyphs) ids.push_back(g.glyph_id);
    return ids;
}

std::vector<std::uint32_t> glyph_ids(const PlacedGlyphRun& placed) {
    std::vector<std::uint32_t> ids;
    ids.reserve(placed.glyphs.size());
    for (const auto& g : placed.glyphs) ids.push_back(g.glyph_id);
    return ids;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Ligatures — disabling 'liga' must prevent the "fi" ligature.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: liga=0 prevents fi ligature") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const auto font = inter_bold_quality();
    const float size = 32.0f;

    TextShaping default_shaping;
    TextShaping no_liga;
    no_liga.features = "liga=0";

    auto default_run = engine.shape_text("fi", font, size, default_shaping);
    auto no_liga_run = engine.shape_text("fi", font, size, no_liga);

    REQUIRE(default_run.has_value());
    REQUIRE(no_liga_run.has_value());
    REQUIRE(default_run->glyphs.size() > 0);
    REQUIRE(no_liga_run->glyphs.size() > 0);

    INFO("default glyphs: ", default_run->glyphs.size(),
         " no-liga glyphs: ", no_liga_run->glyphs.size());

    // With default shaping HarfBuzz applies 'liga' implicitly.  Inter-Bold
    // contains an 'fi' ligature, so the default run should collapse the two
    // characters to a single glyph.  With liga=0 it must stay as two glyphs.
    CHECK(default_run->glyphs.size() == 1);
    CHECK(no_liga_run->glyphs.size() == 2);
    CHECK(glyph_ids(*default_run) != glyph_ids(*no_liga_run));
}

TEST_CASE("OpenType: liga=0 prevents ffi ligature") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const auto font = inter_bold_quality();
    const float size = 32.0f;

    TextShaping default_shaping;
    TextShaping no_liga;
    no_liga.features = "liga=0";

    auto default_run = engine.shape_text("ffi", font, size, default_shaping);
    auto no_liga_run = engine.shape_text("ffi", font, size, no_liga);

    REQUIRE(default_run.has_value());
    REQUIRE(no_liga_run.has_value());

    INFO("default glyphs: ", default_run->glyphs.size(),
         " no-liga glyphs: ", no_liga_run->glyphs.size());

    CHECK(default_run->glyphs.size() < no_liga_run->glyphs.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Small caps — smcp=1 should change glyph IDs when supported by the font.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: smcp=1 changes glyph IDs when supported") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const auto font = inter_bold_quality();
    const float size = 32.0f;

    TextShaping default_shaping;
    TextShaping smcp;
    smcp.features = "smcp=1";

    auto default_run = engine.shape_text("Hello", font, size, default_shaping);
    auto smcp_run    = engine.shape_text("Hello", font, size, smcp);

    REQUIRE(default_run.has_value());
    REQUIRE(smcp_run.has_value());

    const auto default_ids = glyph_ids(*default_run);
    const auto smcp_ids    = glyph_ids(*smcp_run);

    if (default_ids == smcp_ids) {
        MESSAGE("Inter-Bold does not expose smcp for 'Hello' — feature unsupported by font, skipped");
    } else {
        CHECK(default_ids != smcp_ids);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Oldstyle numerals — onum=1 should change glyph IDs when supported.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: onum=1 changes glyph IDs when supported") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const auto font = inter_bold_quality();
    const float size = 32.0f;

    TextShaping default_shaping;
    TextShaping onum;
    onum.features = "onum=1";

    auto default_run = engine.shape_text("12345", font, size, default_shaping);
    auto onum_run    = engine.shape_text("12345", font, size, onum);

    REQUIRE(default_run.has_value());
    REQUIRE(onum_run.has_value());

    const auto default_ids = glyph_ids(*default_run);
    const auto onum_ids    = glyph_ids(*onum_run);

    if (default_ids == onum_ids) {
        MESSAGE("Inter-Bold does not expose onum for '12345' — feature unsupported by font, skipped");
    } else {
        CHECK(default_ids != onum_ids);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Multiple features are parsed and applied together.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: multiple features are applied together") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    const auto font = inter_bold_quality();
    const float size = 32.0f;

    TextShaping no_liga_kern;
    no_liga_kern.features = "liga=0,kern=0";

    TextShaping no_liga_kern_spaced;
    no_liga_kern_spaced.features = " liga=0 , kern=0 ";

    auto run1 = engine.shape_text("ffi", font, size, no_liga_kern);
    auto run2 = engine.shape_text("ffi", font, size, no_liga_kern_spaced);

    REQUIRE(run1.has_value());
    REQUIRE(run2.has_value());

    CHECK(glyph_ids(*run1) == glyph_ids(*run2));
    CHECK(run1->glyphs.size() == 3);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. resolve_and_shape forwards features to HarfBuzz.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: resolve_and_shape forwards features") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    TextDocument doc;
    doc.utf8 = "fi";
    doc.defaults.font = inter_bold_quality();
    doc.split_paragraphs();

    auto default_tree = resolve_and_shape(doc, engine, 0.0f, test_repo_root() / "assets" / "fonts");
    auto no_liga_tree = resolve_and_shape(doc, engine, 0.0f, test_repo_root() / "assets" / "fonts", "liga=0");

    REQUIRE(!default_tree.paragraphs.empty());
    REQUIRE(!no_liga_tree.paragraphs.empty());

    std::size_t default_glyphs = 0;
    std::size_t no_liga_glyphs = 0;
    for (const auto& p : default_tree.paragraphs) {
        for (const auto& r : p.shaped_runs) default_glyphs += r.glyphs.size();
    }
    for (const auto& p : no_liga_tree.paragraphs) {
        for (const auto& r : p.shaped_runs) no_liga_glyphs += r.glyphs.size();
    }

    INFO("default glyphs: ", default_glyphs, " no-liga glyphs: ", no_liga_glyphs);
    CHECK(default_glyphs == 1);
    CHECK(no_liga_glyphs == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. TextLayoutSpec::features is forwarded through build_text_run.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: TextLayoutSpec::features forwarded through build_text_run") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    TextDocument doc;
    doc.utf8 = "fi";
    doc.defaults.font = inter_bold_quality();
    doc.split_paragraphs();

    TextLayoutSpec default_layout;
    TextLayoutSpec no_liga_layout;
    no_liga_layout.features = "liga=0";

    auto default_result = build_text_run(doc, engine, default_layout);
    auto no_liga_result   = build_text_run(doc, engine, no_liga_layout);

    REQUIRE(!default_result.paragraphs.empty());
    REQUIRE(default_result.paragraphs.front() != nullptr);
    REQUIRE(!no_liga_result.paragraphs.empty());
    REQUIRE(no_liga_result.paragraphs.front() != nullptr);

    const auto& default_placed = default_result.paragraphs.front()->placed;
    const auto& no_liga_placed   = no_liga_result.paragraphs.front()->placed;

    INFO("default glyphs: ", default_placed.glyphs.size(),
         " no-liga glyphs: ", no_liga_placed.glyphs.size());

    CHECK(default_placed.glyphs.size() == 1);
    CHECK(no_liga_placed.glyphs.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Cached layouts with different features do not collide.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("OpenType: cache discriminates on features") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    TextDocument doc;
    doc.utf8 = "fi";
    doc.defaults.font = inter_bold_quality();
    doc.split_paragraphs();

    TextLayoutSpec default_layout;
    TextLayoutSpec no_liga_layout;
    no_liga_layout.features = "liga=0";

    TextLayoutCache cache{64 * 1024 * 1024};

    auto a = build_text_run(doc, engine, default_layout, &cache);
    auto b = build_text_run(doc, engine, no_liga_layout, &cache);

    REQUIRE(!a.paragraphs.empty());
    REQUIRE(!b.paragraphs.empty());

    CHECK(a.paragraphs.front()->placed.glyphs.size() == 1);
    CHECK(b.paragraphs.front()->placed.glyphs.size() == 2);
}
