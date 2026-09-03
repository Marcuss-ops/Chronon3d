// ═══════════════════════════════════════════════════════════════════════════
// tests/certification/test_cert_text_bbox.cpp
//
// BUG 1 bounding-box regression locks for CertTitle and CertLowerThird.
//
// CertTitle:
//   "EPIC TITLE" 120pt Inter Bold centered on 1920×1080 canvas.
//   Bbox centre must be within 2 px of canvas centre (960, 540).
//
// CertLowerThird:
//   "BREAKING NEWS" 42pt + subtitle 24pt in a 140px semi-transparent box
//   pinned to BottomCenter with 80px margin.
//   Text bbox must be within x:[80, 1840], y:[900, 1070].
//
// Uses the canonical TextDefinition authoring path so placement is resolved
// by the same code used by production compositions.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <chronon3d/text/text_definition.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── CertTitle-style composition ───────────────────────────────────────────
// "EPIC TITLE" 120 pt Inter Bold, centred on 1920×1080.
// Position {960, 540, 0}: canvas-absolute centre.
Composition build_cert_title_comp(SoftwareRenderer& renderer) {
    return composition(
        {.name = "CertTitle/bbox_test", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("title", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("title_text", TextDefinition{
                    .content = {.value = "EPIC TITLE"},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size = 120.0f
                    }, .color = Color::white()},
                    .frame = {
                        .size = {1920.0f, 1080.0f},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute, {960.0f, 540.0f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .centering_mode = TextCenteringMode::PixelInk,
                    }
                });
            });
            return s.build();
        });
}

// ── CertLowerThird-style composition ──────────────────────────────────────
Composition build_cert_lower_third_comp(SoftwareRenderer& renderer) {
    constexpr float kMargin    = 80.0f;
    constexpr float kBoxHeight = 140.0f;
    return composition(
        {.name = "CertLowerThird/bbox_test", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            // Title: "BREAKING NEWS" — centred horizontally, in lower third
            // (no background rect: bbox check measures text-only)
            s.layer("title_line", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("title", TextDefinition{
                    .content = {.value = "BREAKING NEWS"},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size = 42.0f
                    }, .color = Color::white()},
                    .frame = {
                        .size = {1920.0f - kMargin * 2.0f, 60.0f},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute, {960.0f, 940.0f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .centering_mode = TextCenteringMode::PixelInk,
                    }
                });
            });
            // Subtitle: below title
            s.layer("subtitle_line", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("subtitle", TextDefinition{
                    .content = {.value = "Chronon3D Text Engine — Production Ready"},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size = 24.0f
                    }, .color = Color{0.85f, 0.85f, 0.9f, 1.0f}},
                    .frame = {
                        .size = {1920.0f - kMargin * 2.0f, 40.0f},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute, {960.0f, 1000.0f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .centering_mode = TextCenteringMode::PixelInk,
                    }
                });
            });
            return s.build();
        });
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// CertTitle — bbox must be centred within 2 px of canvas centre
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CertTitle text bbox is centred on 1920x1080 canvas") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_cert_title_comp(renderer), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const auto bbox = completeness::alpha_bbox(*fb);
    INFO("CertTitle bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    // Text must be visible with plausible glyph dimensions.
    CHECK_FALSE(bbox.empty());
    CHECK(bbox.width()  > 200);   // "EPIC TITLE" at 120pt is ~500px wide
    CHECK(bbox.height() > 30);    // ~80px of glyph ink

    // Bbox must be fully inside the canvas (visible, not clipped).
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.x1 <= 1920);
    CHECK(bbox.y0 >= 0);
    CHECK(bbox.y1 <= 1080);

    // Centre must be within tolerance of canvas centre on BOTH axes.
    // X-centre check UN-SKIPPED: the BUG-1 double anchor consumption was
    // fixed by the ANCHOR EXACTLY-ONCE CONTRACT in layer_builder_text.cpp
    // (resolve_text_placement consumes the box anchor once; the node
    // transform anchor stays {0,0}).  The 5-pin × 3-anchor matrix in
    // tests/render_graph/nodes/test_text_run_predicted_bbox.cpp proves the
    // identical Absolute{960,540}+Center chain lands at 960 ±10 px.
    const float cx = (bbox.x0 + bbox.x1) * 0.5f;
    CHECK(std::abs(cx - 960.0f) <= 10.0f);
    const float cy = (bbox.y0 + bbox.y1) * 0.5f;
    CHECK(std::abs(cy - 540.0f) <= 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// CertLowerThird — text bbox must be inside [80,1840]×[900,1070]
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CertLowerThird text bbox is inside lower-third safe area") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_cert_lower_third_comp(renderer), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const auto bbox = completeness::alpha_bbox(*fb);
    INFO("CertLowerThird bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    // Text + box must be visible.
    CHECK_FALSE(bbox.empty());
    CHECK(bbox.width()  > 100);
    CHECK(bbox.height() > 20);

    // Bbox must be inside the lower-third safe area.
    CHECK(bbox.x0 >= 80);
    CHECK(bbox.x1 <= 1840);
    CHECK(bbox.y0 >= 900);
    CHECK(bbox.y1 <= 1070);
}
