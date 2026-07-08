#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/golden_test.hpp>

#include <filesystem>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

const std::filesystem::path kGoldenDir    = "test_renders/golden/rounded_rect";
const std::filesystem::path kArtifactDir  = "test_renders/artifacts/rounded_rect";

GoldenTestConfig make_config() {
    return {
        .golden_directory  = kGoldenDir,
        .artifact_directory = kArtifactDir,
        .threshold          = {},
        .mode               = golden_mode_from_environment(),
    };
}

void verify_golden_or_create(const Framebuffer& rendered, const std::string& name) {
    const auto result = verify_golden(rendered, name, make_config());
    INFO(result.message);
    CHECK(result.passed);
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 1: Basic rounded_rect with solid fill
// ═══════════════════════════════════════════════════════════════════════

Composition make_basic_rounded_rect() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {600.0f, 300.0f},
                    .radius = 24.0f,
                    .color  = Color::from_hex("#2563eb"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: basic solid fill") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_basic_rounded_rect(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_basic_solid");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 2: rounded_rect with linear gradient fill
// ═══════════════════════════════════════════════════════════════════════

Composition make_gradient_rounded_rect() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {600.0f, 300.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#1e3a5f")},
                            {1.0f, Color::from_hex("#3b82f6")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: linear gradient fill") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_gradient_rounded_rect(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_gradient_fill");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 3: rounded_rect with radial gradient fill
// ═══════════════════════════════════════════════════════════════════════

Composition make_radial_rounded_rect() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 350.0f},
                    .radius = 32.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::radial(
                        {0.5f, 0.5f}, 0.5f,
                        {
                            {0.0f, Color::from_hex("#7c3aed")},
                            {0.5f, Color::from_hex("#a78bfa")},
                            {1.0f, Color::from_hex("#1e1b4b")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: radial gradient fill") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_radial_rounded_rect(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_radial_fill");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 4: rounded_rect with stroke (center alignment)
// ═══════════════════════════════════════════════════════════════════════

Composition make_stroke_center() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 250.0f},
                    .radius = 20.0f,
                    .color  = Color::from_hex("#1e3a5f"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {.enabled = true, .color = Color::from_hex("#3b82f6"), .width = 4.0f, .alignment = StrokeAlignment::Center},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: stroke center alignment") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_stroke_center(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_stroke_center");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 5: rounded_rect with stroke (inside alignment)
// ═══════════════════════════════════════════════════════════════════════

Composition make_stroke_inside() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 250.0f},
                    .radius = 20.0f,
                    .color  = Color::from_hex("#065f46"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {.enabled = true, .color = Color::from_hex("#34d399"), .width = 4.0f, .alignment = StrokeAlignment::Inside},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: stroke inside alignment") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_stroke_inside(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_stroke_inside");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 6: rounded_rect with stroke (outside alignment)
// ═══════════════════════════════════════════════════════════════════════

Composition make_stroke_outside() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 250.0f},
                    .radius = 20.0f,
                    .color  = Color::from_hex("#831843"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {.enabled = true, .color = Color::from_hex("#f472b6"), .width = 4.0f, .alignment = StrokeAlignment::Outside},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: stroke outside alignment") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_stroke_outside(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_stroke_outside");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 7: rounded_rect with thick stroke (outside, 12px)
// ═══════════════════════════════════════════════════════════════════════

Composition make_thick_stroke() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {400.0f, 200.0f},
                    .radius = 28.0f,
                    .color  = Color::from_hex("#0f172a"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {.enabled = true, .color = Color::from_hex("#f59e0b"), .width = 12.0f, .alignment = StrokeAlignment::Outside},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: thick stroke outside") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_thick_stroke(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_thick_stroke");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 8: rounded_rect with gradient fill + stroke
// ═══════════════════════════════════════════════════════════════════════

Composition make_gradient_fill_with_stroke() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {550.0f, 280.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::linear(
                        {0.0f, 0.0f}, {1.0f, 1.0f},
                        {
                            {0.0f, Color::from_hex("#7c3aed")},
                            {1.0f, Color::from_hex("#fbbf24")},
                        }),
                    .stroke = {.enabled = true, .color = Color::from_hex("#ffffff"), .width = 3.0f, .alignment = StrokeAlignment::Center},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: gradient fill with stroke") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_gradient_fill_with_stroke(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_gradient_fill_stroke");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 9: Rotated rounded_rect with gradient + stroke
// ═══════════════════════════════════════════════════════════════════════

Composition make_rotated_with_stroke() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("rotated", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rotate({0.0f, 0.0f, 20.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 200.0f},
                    .radius = 22.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#be123c")},
                            {1.0f, Color::from_hex("#fda4af")},
                        }),
                    .stroke = {.enabled = true, .color = Color::from_hex("#881337"), .width = 4.0f, .alignment = StrokeAlignment::Outside},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: rotated with gradient and stroke") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_rotated_with_stroke(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_rotated_stroke");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 10: Composite scene — multiple rounded_rects
// ═══════════════════════════════════════════════════════════════════════

Composition make_composite_rounded_rects() {
    return Composition({.width = 1920, .height = 1080, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.11f, 0.14f, 1.0f});
            });

            // Top-left: solid fill
            s.layer("card1", [](LayerBuilder& l) {
                l.position({-480.0f, -270.0f, 0.0f});
                l.rounded_rect("r1", {
                    .size   = {400.0f, 220.0f},
                    .radius = 16.0f,
                    .color  = Color::from_hex("#1e40af"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                });
            });

            // Top-center: gradient fill
            s.layer("card2", [](LayerBuilder& l) {
                l.position({0.0f, -270.0f, 0.0f});
                l.rounded_rect("r2", {
                    .size   = {400.0f, 220.0f},
                    .radius = 20.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {{0.0f, Color::from_hex("#065f46")}, {1.0f, Color::from_hex("#34d399")}}),
                });
            });

            // Top-right: stroke center
            s.layer("card3", [](LayerBuilder& l) {
                l.position({480.0f, -270.0f, 0.0f});
                l.rounded_rect("r3", {
                    .size   = {400.0f, 220.0f},
                    .radius = 16.0f,
                    .color  = Color::from_hex("#0f172a"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {.enabled = true, .color = Color::from_hex("#38bdf8"), .width = 3.0f, .alignment = StrokeAlignment::Center},
                });
            });

            // Bottom-left: gradient + stroke
            s.layer("card4", [](LayerBuilder& l) {
                l.position({-480.0f, 270.0f, 0.0f});
                l.rounded_rect("r4", {
                    .size   = {400.0f, 220.0f},
                    .radius = 18.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::radial(
                        {0.5f, 0.5f}, 0.45f,
                        {{0.0f, Color::from_hex("#7c3aed")}, {1.0f, Color::from_hex("#1e1b4b")}}),
                    .stroke = {.enabled = true, .color = Color::from_hex("#c084fc"), .width = 2.5f, .alignment = StrokeAlignment::Outside},
                });
            });

            // Bottom-center: thick outside stroke
            s.layer("card5", [](LayerBuilder& l) {
                l.position({0.0f, 270.0f, 0.0f});
                l.rounded_rect("r5", {
                    .size   = {400.0f, 220.0f},
                    .radius = 24.0f,
                    .color  = Color::from_hex("#1e1b4b"),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {.enabled = true, .color = Color::from_hex("#f59e0b"), .width = 8.0f, .alignment = StrokeAlignment::Outside},
                });
            });

            // Bottom-right: rotated with gradient
            s.layer("card6", [](LayerBuilder& l) {
                l.position({480.0f, 270.0f, 0.0f});
                l.rotate({0.0f, 0.0f, -15.0f});
                l.rounded_rect("r6", {
                    .size   = {400.0f, 220.0f},
                    .radius = 20.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::linear(
                        {0.0f, 0.0f}, {1.0f, 1.0f},
                        {{0.0f, Color::from_hex("#be123c")}, {1.0f, Color::from_hex("#fda4af")}}),
                    .stroke = {.enabled = true, .color = Color::from_hex("#881337"), .width = 3.0f, .alignment = StrokeAlignment::Outside},
                });
            });

            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: composite six-card scene") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_composite_rounded_rects(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_composite_scene");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 11: rounded_rect with gradient stroke
// ═══════════════════════════════════════════════════════════════════════

Composition make_gradient_stroke() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            // Transparent fill, only the stroke is visible with its own gradient
            s.layer("gradient_stroke", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 280.0f},
                    .radius = 24.0f,
                    .color  = Color{0.0f, 0.0f, 0.0f, 0.0f},  // transparent fill
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .stroke = {
                        .enabled = true,
                        .width = 6.0f,
                        .alignment = StrokeAlignment::Center,
                        .gradient = graphics::FillStyle::linear(
                            {0.0f, 0.5f}, {1.0f, 0.5f},
                            {
                                {0.0f, Color::from_hex("#ef4444")},
                                {0.33f, Color::from_hex("#f59e0b")},
                                {0.66f, Color::from_hex("#10b981")},
                                {1.0f, Color::from_hex("#3b82f6")},
                            }),
                    },
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: gradient stroke") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_gradient_stroke(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_gradient_stroke");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 12: Clipped rounded_rect (partially off-screen)
// ═══════════════════════════════════════════════════════════════════════

Composition make_clipped_rounded_rect() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            // Positioned partially off the top-left corner
            s.layer("clipped", [](LayerBuilder& l) {
                l.position({-250.0f, -150.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {500.0f, 400.0f},
                    .radius = 28.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = graphics::FillStyle::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#1e3a5f")},
                            {1.0f, Color::from_hex("#3b82f6")},
                        }),
                    .stroke = {.enabled = true, .color = Color::from_hex("#60a5fa"), .width = 3.0f, .alignment = StrokeAlignment::Outside},
                });
            });
            return s.build();
        });
}

TEST_CASE("RoundedRectGolden: clipped off-screen") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render(make_clipped_rounded_rect(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "rounded_rect_clipped");
}

}  // namespace
