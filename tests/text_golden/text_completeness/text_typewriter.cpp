// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_typewriter.cpp
//
// P0-7: Typewriter / Kinetic Typography — frame-by-frame verification.
//
// The typewriter effect progressively reveals text character by character.
// We approximate this via string slicing at composition-build time
// (same approach as ae_02_typewriter.cpp).
//
// Cases:
//   1. Frame 0 (1 char) has less ink than frame 30 (full text)
//   2. Visible pixels grow monotonically: f0 < f10 < f20 < f30
//   3. No ghost glyphs: frame 0 has significantly fewer pixels than final
//   4. Final frame has maximum ink coverage
//   5. Determinism: same frame → same hash across renders
//   6. Frame 0 has first character ink (reveal starts immediately)
//
// Uses string slicing (not per-glyph opacity animation) which is the
// current approximation used by the golden test suite.  When per-glyph
// opacity animation is available, these tests should be updated.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

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

constexpr const char* kTypewriterFull = "TYPEWRITER REVEAL TEST";

// Frame-by-frame substring: simulates progressive character reveal.
std::string typewriter_text(std::size_t frame_idx, std::size_t total_frames = 30) {
    const std::size_t n = std::strlen(kTypewriterFull);
    if (frame_idx == 0) return std::string(1, kTypewriterFull[0]);
    const std::size_t chars = std::max<std::size_t>(
        1, (n * frame_idx) / total_frames);
    return std::string(kTypewriterFull, std::min(chars, n));
}

Composition build_typewriter_composition(
    SoftwareRenderer& renderer,
    std::size_t frame_idx
) {
    return composition(
        {.name = "TextTypewriter/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("tw_layer", [&renderer, frame_idx](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("tw_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = typewriter_text(frame_idx)},.placement = TextPlacement{TextPlacementKind::Absolute, {100.0f, 540.0f}},.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        },.layout = {
                            .box = {1800.0f, 400.0f},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Middle
                        },.appearance = {.color = Color::white()},}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Frame 0 has less ink than final frame ═══════════════════════
TEST_CASE("TextTypewriter 01: frame 0 has less ink than final") {
    auto r0 = test::make_renderer();
    auto rF = test::make_renderer();

    auto fb0 = r0.render(build_typewriter_composition(r0, 0), Frame{0});
    auto fbF = rF.render(build_typewriter_composition(rF, 30), Frame{30});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fbF != nullptr);

    const int vis0 = count_visible_pixels(*fb0);
    const int visF = count_visible_pixels(*fbF);
    INFO("frame0 visible=", vis0, " frame30 visible=", visF);

    // Both must have some ink.
    CHECK(vis0 > 0);
    CHECK(visF > 0);
    // Final frame must have significantly more ink than frame 0.
    CHECK(visF > vis0 * 3);
}

// ═══ Test 2 — Visible pixels grow monotonically ═══════════════════════════
// Render at f0, f10, f20, f30 and verify ink increases at each step.
TEST_CASE("TextTypewriter 02: visible pixels grow monotonically") {
    int prev_vis = 0;
    for (std::size_t f : {0u, 10u, 20u, 30u}) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_typewriter_composition(renderer, f), Frame{f});
        REQUIRE(fb != nullptr);

        const int vis = count_visible_pixels(*fb);
        INFO("frame=", f, " visible=", vis, " prev=", prev_vis);

        CHECK(vis > 0);
        if (prev_vis > 0) {
            // Ink should grow or stay the same (monotonic).
            CHECK(vis >= prev_vis);
        }
        prev_vis = vis;
    }
}

// ═══ Test 3 — No ghost glyphs: frame 0 ink << final frame ink ═════════════
TEST_CASE("TextTypewriter 03: no ghost glyphs frame 0 vs final") {
    auto r0 = test::make_renderer();
    auto rF = test::make_renderer();

    auto fb0 = r0.render(build_typewriter_composition(r0, 0), Frame{0});
    auto fbF = rF.render(build_typewriter_composition(rF, 30), Frame{30});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fbF != nullptr);

    const int vis0 = count_visible_pixels(*fb0);
    const int visF = count_visible_pixels(*fbF);
    const AlphaBBox bbox0 = alpha_bbox(*fb0);
    const AlphaBBox bboxF = alpha_bbox(*fbF);

    INFO("frame0: visible=", vis0, " bbox_w=", bbox0.width());
    INFO("frame30: visible=", visF, " bbox_w=", bboxF.width());

    // Frame 0 (1 char) should have much less ink than frame 30 (full text).
    CHECK(vis0 < visF / 3);

    // Frame 0 bbox should be much narrower than final.
    CHECK(bbox0.width() < bboxF.width() / 2);
}

// ═══ Test 4 — Final frame has maximum ink coverage ════════════════════════
TEST_CASE("TextTypewriter 04: final frame has maximum ink") {
    int max_vis = 0;
    std::size_t max_frame = 0;

    for (std::size_t f : {0u, 5u, 10u, 15u, 20u, 25u, 30u}) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_typewriter_composition(renderer, f), Frame{f});
        REQUIRE(fb != nullptr);

        const int vis = count_visible_pixels(*fb);
        if (vis > max_vis) {
            max_vis = vis;
            max_frame = f;
        }
    }

    INFO("max_vis=", max_vis, " at frame=", max_frame);

    // The maximum should be at frame 30 (full text).
    CHECK(max_frame == 30);
}

// ═══ Test 5 — Determinism: same frame → same hash ═════════════════════════
TEST_CASE("TextTypewriter 05: deterministic across renders") {
    auto r1 = test::make_renderer();
    auto r2 = test::make_renderer();

    auto fb1 = r1.render(build_typewriter_composition(r1, 15), Frame{15});
    auto fb2 = r2.render(build_typewriter_composition(r2, 15), Frame{15});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    CHECK(count_visible_pixels(*fb1) == count_visible_pixels(*fb2));
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

// ═══ Test 6 — Before start (frame 0 with empty content) ══════════════════
// The typewriter starts at frame 0 with 1 character.  Verify that even
// at frame 0, there's at least some ink (the first character).
TEST_CASE("TextTypewriter 06: frame 0 has first character ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_typewriter_composition(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);

    const int vis = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("frame0: visible=", vis, " bbox_w=", bbox.width());

    // Frame 0 should have at least 1 character worth of ink.
    CHECK(vis > 0);
    CHECK_FALSE(bbox.empty());
    // The first character "T" at 96pt should produce some width.
    CHECK(bbox.width() > 20);
}
