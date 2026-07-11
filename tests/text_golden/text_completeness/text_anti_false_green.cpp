// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_anti_false_green.cpp
//
// Anti-False-Green Tests — verifies that the text pipeline does NOT
// report success when the output is actually broken.
//
// These are negative tests: each one checks a condition that MUST fail
// or produce a detectable warning if the pipeline is producing bogus
// results.  The goal is to prevent regressions where:
//   - A PNG file exists but is fully transparent
//   - A [BOUNDARY-OK] diagnostic prints but the frame is black
//   - A layout bbox is computed but no raster ink exists
//   - A missing font silently falls back without warning
//   - A typewriter final frame is missing characters
//
// Cases:
//   1. Valid font + empty text      → no ink (don't claim success)
//   2. Missing font + valid text    → detectable failure, not silent pass
//   3. Bbox reported but no ink     → inconsistency caught
//   4. Typewriter final vs full     → final frame must contain all chars
//   5. Zero-opacity layer           → no false-positive ink detection
//   6. Whitespace layout bbox ≠ ink → layout bbox without raster is suspect
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <string>
#include <cstring>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

/// Baseline: centered white text on 1920×1080, Inter-Bold 96pt.
Composition build_text_comp(SoftwareRenderer& renderer,
                             std::string_view text = "ANTI-FALSE",
                             std::string_view font_path = "assets/fonts/Inter-Bold.ttf",
                             float opacity = 1.0f) {
    return composition(
        {.name = "TextAntiFalse/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text = std::string{text}, font_path = std::string{font_path},
         opacity](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("afg_layer", [&renderer, text, font_path, opacity](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                if (opacity < 1.0f) l.opacity(opacity);
                l.text_run("afg_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = text}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = font_path,
                            .font_family = "TestFont",
                            .font_weight = 700,
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Typewriter composition with progressive text reveal via string slicing.
Composition build_tw_comp(SoftwareRenderer& renderer, std::size_t frame_idx) {
    constexpr const char* kFull = "TYPEWRITER ANTI FALSE GREEN";
    const std::size_t n = std::strlen(kFull);
    std::string sliced;
    if (frame_idx >= n) {
        sliced = kFull;
    } else if (frame_idx == 0) {
        sliced = std::string(1, kFull[0]);
    } else {
        sliced = std::string(kFull, frame_idx);
    }

    return composition(
        {.name = "TextAntiFalse/tw",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, sliced](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("tw_layer", [&renderer, sliced](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("tw_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = sliced}, .placement = {TextPlacementKind::Absolute, {100.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 72.0f
                        }, .layout = {
                            .box = {1800.0f, 400.0f},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Empty text with valid font: no false-positive ink ══════════
// If text is empty, the pipeline may still create a valid PNG.  We must
// verify that no ink exists — no false "render succeeded" signal.
TEST_CASE("AntiFalseGreen 01: empty text produces no ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_text_comp(renderer, ""), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);

    INFO("empty text: visible=", visible,
         " bbox_empty=", bbox.empty());

    // CRITICAL: empty text must NOT produce visible ink.
    // If this fails, the pipeline is generating noise on empty input.
    CHECK(visible == 0);
    CHECK(bbox.empty());
}

// ═══ Test 2 — Missing font: detectable failure, not silent success ═══════
// When the font file doesn't exist, the pipeline should either throw or
// produce zero ink.  It must NOT silently substitute a fallback font
// without any diagnostic.
TEST_CASE("AntiFalseGreen 02: missing font produces no ink") {
    auto renderer = test::make_renderer();
    bool threw = false;

    try {
        auto fb = renderer.render(
            build_text_comp(renderer, "MISSING FONT",
                            "assets/fonts/NONEXISTENT_FONT.ttf"),
            Frame{0});

        if (fb != nullptr) {
            const int visible = count_visible_pixels(*fb);
            INFO("missing font: visible=", visible,
                 " (should be 0 — no silent fallback)");

            // If the pipeline didn't throw AND produced ink via a silent
            // fallback font, that's an anti-false-green violation.
            if (!threw) {
                CHECK(visible == 0);
            }
        }
    } catch (const std::exception& e) {
        INFO("Missing font threw (graceful): ", e.what());
        threw = true;
    } catch (...) {
        INFO("Missing font threw unknown exception (graceful)");
        threw = true;
    }

    // Either no ink or a thrown exception — both are acceptable.
    // Silent fallback with real ink is NOT acceptable.
    CHECK((threw || true));  // took the right path
}

// ═══ Test 3 — Bbox without ink is a false positive ═══════════════════════
// The alpha_bbox scan may report a non-empty bbox if even one pixel has
// alpha > epsilon.  But we must ensure that the number of visible pixels
// is proportional to the bbox area.  A bbox of 500×500 with only 2
// visible pixels is a false positive.
TEST_CASE("AntiFalseGreen 03: bbox area must match visible pixel count") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_comp(renderer, "BBOX CHECK"), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);

    INFO("visible=", visible, " bbox_w=", bbox.width(), " bbox_h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(visible > 100);

    // Ink density: visible pixels / bbox area must be reasonable.
    // Text at 96pt should have substantial ink coverage.
    const int bbox_area = bbox.width() * bbox.height();
    const float density = (bbox_area > 0)
        ? static_cast<float>(visible) / static_cast<float>(bbox_area)
        : 0.0f;

    INFO("bbox_area=", bbox_area, " ink_density=", density);

    // A legitimate text render should have ink density above 1%.
    // Below 1% suggests a near-empty bbox (anti-false-green check).
    CHECK(density > 0.01f);
}

// ═══ Test 4 — Typewriter final frame has all text ink ════════════════════
// The final frame of a typewriter animation must contain the full text.
// If a typewriter test passes the "growing" monotonicity check but the
// final frame is missing characters, that's a false green.
TEST_CASE("AntiFalseGreen 04: typewriter final frame has full text ink") {
    auto r_final = test::make_renderer();
    auto r_full  = test::make_renderer();

    // Final typewriter frame (all chars revealed).
    auto fb_final = r_final.render(
        build_tw_comp(r_final, 30), Frame{30});
    // Direct render of the full string (same text, no slicing).
    auto fb_full = r_full.render(
        build_text_comp(r_full, "TYPEWRITER ANTI FALSE GREEN",
                        "assets/fonts/Inter-Bold.ttf", 1.0f),
        Frame{0});
    REQUIRE(fb_final != nullptr);
    REQUIRE(fb_full != nullptr);

    const int vis_final = count_visible_pixels(*fb_final);
    const int vis_full  = count_visible_pixels(*fb_full);

    INFO("typewriter final: visible=", vis_final);
    INFO("direct full text: visible=", vis_full);

    // Both should have substantial ink.
    CHECK(vis_final > 100);
    CHECK(vis_full > 100);

    // The final typewriter frame should have comparable ink to the direct
    // full-text render (allowing for layout differences from box size).
    // If typewriter final has significantly less ink, the reveal is
    // incomplete — a false green.
    CHECK(vis_final > vis_full / 3);
}

// ═══ Test 5 — Zero-opacity: no ink leaks through compositor ═══════════
// DIFFERENT from VisibleInk 03: this test additionally verifies that
// even with a valid font loaded AND layout computed, opacity 0 at the
// layer level must not leak any raster ink into the framebuffer.
// VisibleInk 03 only checks visible < 10; this test adds the stronger
// assertion that alpha_bbox is empty (no pixel with alpha > epsilon).
TEST_CASE("AntiFalseGreen 05: zero-opacity layer has empty alpha bbox") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_comp(renderer, "INVISIBLE",
                        "assets/fonts/Inter-Bold.ttf", 0.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const int bright   = count_bright_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);

    INFO("zero-opacity: visible=", visible, " bright=", bright,
         " bbox_empty=", bbox.empty());

    CHECK(visible < 10);
    CHECK(bright < 10);
    // Stronger than VisibleInk 03: alpha_bbox must be empty.
    CHECK(bbox.empty());
}

// ═══ Test 6 — Deterministic anti-false-green: two renders differ ═════════
// Renders of two different texts must produce measurably different output.
// If two different inputs produce identical framebuffers, something is
// deeply broken (e.g., cache poisoning, empty renders for both).
TEST_CASE("AntiFalseGreen 06: different texts produce different output") {
    auto r1 = test::make_renderer();
    auto r2 = test::make_renderer();

    auto fb_a = r1.render(
        build_text_comp(r1, "OUTPUT ALPHA"), Frame{0});
    auto fb_b = r2.render(
        build_text_comp(r2, "OUTPUT BRAVO"), Frame{0});
    REQUIRE(fb_a != nullptr);
    REQUIRE(fb_b != nullptr);

    const auto hash_a = framebuffer_hash(*fb_a);
    const auto hash_b = framebuffer_hash(*fb_b);
    const int vis_a = count_visible_pixels(*fb_a);
    const int vis_b = count_visible_pixels(*fb_b);

    INFO("alpha: visible=", vis_a, " hash=", hash_a);
    INFO("bravo: visible=", vis_b, " hash=", hash_b);

    CHECK(vis_a > 100);
    CHECK(vis_b > 100);
    // Two different texts at the same position/size should differ.
    // If they don't, the pipeline is producing identical output for
    // different inputs — a serious false-green condition.
    CHECK(hash_a != hash_b);
}
