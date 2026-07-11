// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_visible_ink.cpp
//
// P0-1: Text Visible Ink — anti-false-positive regression tests.
//
// Goal: ensure that when text is expected to be visible, real ink pixels
// exist in the framebuffer — not just a valid PNG with transparent or
// empty content.
//
// Cases:
//   1. White text on black bg        → visible ink (alpha + luminance)
//   2. Black text on transparent bg  → alpha channel present
//   3. Text with opacity 0           → NO visible ink (anti-false-positive)
//   4. Text positioned off-canvas    → NO visible ink (out of area)
//   5. Whitespace text               → minimal ink
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

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

Composition build_text_composition(
    SoftwareRenderer& renderer,
    std::string_view text = "HELLO WORLD",
    Color color = Color::white(),
    float opacity = 1.0f,
    Vec3 position = {960.0f, 540.0f, 0.0f}
) {
    return composition(
        {.name = "TextVisibleInk/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, color, opacity, position](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("text_layer", [&renderer, text, color, opacity, position](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                if (opacity < 1.0f) {
                    l.opacity(opacity);
                }
                l.text_run("ink_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = std::string{text}},.position = position,.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 120.0f
                        },.layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },.appearance = {.color = color},}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("VisibleInk 01: white text on black has real ink pixels") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_composition(renderer, "HELLO WORLD", Color::white(), 1.0f),
        Frame{0});
    REQUIRE(fb != nullptr);
    const int bright = count_bright_pixels(*fb, 0.05f);
    const int visible = count_visible_pixels(*fb, 0.01f);
    INFO("bright_pixels=", bright, " visible_alpha_pixels=", visible);
    CHECK(bright > 100);
    CHECK(visible > 100);
}

TEST_CASE("VisibleInk 02: black text on transparent has alpha ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_composition(renderer, "BLACK INK", Color{0.0f, 0.0f, 0.0f, 1.0f}, 1.0f),
        Frame{0});
    REQUIRE(fb != nullptr);
    const int visible = count_visible_pixels(*fb, 0.01f);
    INFO("alpha_visible_pixels=", visible);
    CHECK(visible > 100);
    const float m_alpha = mean_alpha(*fb);
    INFO("mean_alpha=", m_alpha);
    CHECK(m_alpha > 0.001f);
}

TEST_CASE("VisibleInk 03: text with opacity 0 is invisible") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_composition(renderer, "INVISIBLE", Color::white(), 0.0f),
        Frame{0});
    REQUIRE(fb != nullptr);
    const int visible = count_visible_pixels(*fb, 0.01f);
    const int bright = count_bright_pixels(*fb, 0.05f);
    INFO("visible_alpha_pixels=", visible, " bright_pixels=", bright);
    CHECK(visible < 10);
    CHECK(bright < 10);
}

TEST_CASE("VisibleInk 04: text off-canvas has no visible ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_composition(renderer, "OFF SCREEN", Color::white(), 1.0f,
                               Vec3{99999.0f, 99999.0f, 0.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);
    const int visible = count_visible_pixels(*fb, 0.01f);
    INFO("visible_alpha_pixels=", visible);
    CHECK(visible < 10);
}

TEST_CASE("VisibleInk 05: whitespace text has minimal ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_text_composition(renderer, "\u00A0", Color::white(), 1.0f),
        Frame{0});
    REQUIRE(fb != nullptr);
    const int visible = count_visible_pixels(*fb, 0.01f);
    INFO("visible_alpha_pixels=", visible);
    CHECK(visible < 50);
}
