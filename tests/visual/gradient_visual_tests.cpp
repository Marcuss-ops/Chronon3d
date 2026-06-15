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

const std::filesystem::path kGoldenDir    = "test_renders/golden/gradients";
const std::filesystem::path kArtifactDir  = "test_renders/artifacts/gradients";

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

// ── Helper: manual framebuffer fill with GradientDefinition ───────────

Framebuffer render_gradient_definition_to_fb(
    const graphics::GradientDefinition& def,
    int w, int h)
{
    Framebuffer fb(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Map pixel to gradient-local normalised space
            f32 gx = static_cast<f32>(x) / static_cast<f32>(w);
            f32 gy = static_cast<f32>(y) / static_cast<f32>(h);

            Color linear;
            if (def.type == graphics::GradientType::Radial) {
                linear = graphics::sample_gradient_radial(def, {gx, gy});
            } else {
                // Linear: compute t from geometry
                Vec2 dir = def.end - def.start;
                f32 denom = dir.x * dir.x + dir.y * dir.y;
                f32 t = (denom > 1e-6f)
                    ? ((gx - def.start.x) * dir.x + (gy - def.start.y) * dir.y) / denom
                    : 0.0f;
                linear = graphics::sample_gradient(def, t);
            }
            fb.set_pixel(x, y, linear);
        }
    }
    return fb;
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 1: Linear horizontal gradient
// ═══════════════════════════════════════════════════════════════════════

Composition make_linear_horizontal() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {800.0f, 300.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#1e3a5f")},
                            {0.5f, Color::from_hex("#2563eb")},
                            {1.0f, Color::from_hex("#7dd3fc")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: linear horizontal") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_linear_horizontal(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_linear_horizontal");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 2: Linear vertical gradient
// ═══════════════════════════════════════════════════════════════════════

Composition make_linear_vertical() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {300.0f, 400.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.5f, 0.0f}, {0.5f, 1.0f},
                        {
                            {0.0f, Color::from_hex("#065f46")},
                            {0.5f, Color::from_hex("#10b981")},
                            {1.0f, Color::from_hex("#a7f3d0")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: linear vertical") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_linear_vertical(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_linear_vertical");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 3: Linear diagonal gradient
// ═══════════════════════════════════════════════════════════════════════

Composition make_linear_diagonal() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {600.0f, 400.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.0f}, {1.0f, 1.0f},
                        {
                            {0.0f, Color::from_hex("#7c3aed")},
                            {1.0f, Color::from_hex("#fbbf24")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: linear diagonal") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_linear_diagonal(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_linear_diagonal");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 4: Radial gradient centered
// ═══════════════════════════════════════════════════════════════════════

Composition make_radial_centered() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("orb", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.circle("c", {
                    .radius = 200.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::radial(
                        {0.5f, 0.5f}, 0.5f,
                        {
                            {0.0f, Color::from_hex("#fef08a")},
                            {0.4f, Color::from_hex("#f59e0b")},
                            {1.0f, Color::from_hex("#7c2d12")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: radial centered") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_radial_centered(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_radial_centered");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 5: Radial gradient offset center
// ═══════════════════════════════════════════════════════════════════════

Composition make_radial_offset() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("orb", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.circle("c", {
                    .radius = 200.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::radial(
                        {0.25f, 0.3f}, 0.6f,
                        {
                            {0.0f, Color::from_hex("#e0f2fe")},
                            {0.5f, Color::from_hex("#0284c7")},
                            {1.0f, Color::from_hex("#082f49")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: radial offset") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_radial_offset(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_radial_offset");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 6: Multiple colour stops (4 stops)
// ═══════════════════════════════════════════════════════════════════════

Composition make_multiple_stops() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("swatch", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {800.0f, 250.0f},
                    .radius = 16.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#ef4444")},   // red
                            {0.33f, Color::from_hex("#f59e0b")},  // amber
                            {0.66f, Color::from_hex("#10b981")},  // emerald
                            {1.0f, Color::from_hex("#3b82f6")},   // blue
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: multiple stops") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_multiple_stops(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_multiple_stops");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 7: Alpha stops (varying opacity)
// ═══════════════════════════════════════════════════════════════════════

Composition make_alpha_stops() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            // Three overlapping shapes with different alpha gradients
            for (int i = 0; i < 3; ++i) {
                s.layer("strip_" + std::to_string(i), [i](LayerBuilder& l) {
                    f32 x_offset = (static_cast<f32>(i) - 1.0f) * 200.0f;
                    l.position({x_offset, 0.0f, 0.0f});
                    l.rounded_rect("r", {
                        .size   = {180.0f, 350.0f},
                        .radius = 20.0f,
                        .color  = Color::white(),
                        .pos    = {0.0f, 0.0f, 0.0f},
                        .fill   = Fill::linear(
                            {0.5f, 0.0f}, {0.5f, 1.0f},
                            {
                                {0.0f, Color{1.0f, 0.3f, 0.3f, 1.0f}},
                                {0.5f, Color{1.0f, 0.3f, 0.3f, 0.1f}},
                                {1.0f, Color{0.3f, 0.3f, 1.0f, 1.0f}},
                            }),
                    });
                });
            }
            return s.build();
        });
}

TEST_CASE("GradientGolden: alpha stops") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_alpha_stops(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_alpha_stops");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 8: Transformed shape (rotated rectangle with gradient)
// ═══════════════════════════════════════════════════════════════════════

Composition make_transformed_shape() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            s.layer("rotated_card", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rotate({0.0f, 0.0f, 25.0f});  // 25° rotation around Z
                l.rounded_rect("r", {
                    .size   = {500.0f, 200.0f},
                    .radius = 20.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#831843")},
                            {1.0f, Color::from_hex("#f472b6")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: transformed shape") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_transformed_shape(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_transformed_shape");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 9: Clipped shape (partially outside framebuffer)
// ═══════════════════════════════════════════════════════════════════════

Composition make_clipped_shape() {
    return Composition({.width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });
            // Shape positioned partially off-screen (top-left corner)
            s.layer("clipped", [](LayerBuilder& l) {
                l.position({-300.0f, -150.0f, 0.0f});
                l.circle("c", {
                    .radius = 350.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::radial(
                        {0.5f, 0.5f}, 0.5f,
                        {
                            {0.0f, Color::from_hex("#22d3ee")},
                            {1.0f, Color::from_hex("#1e3a5f")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: clipped shape") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_clipped_shape(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_clipped_shape");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 10: Spread Repeat (manual framebuffer — GradientDefinition)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("GradientGolden: spread Repeat (GradientDefinition)") {
    auto def = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::from_hex("#1e3a5f")},
            {0.5f, Color::from_hex("#f59e0b")},
            {1.0f, Color::from_hex("#1e3a5f")},
        },
        graphics::GradientSpread::Repeat);

    Framebuffer fb = render_gradient_definition_to_fb(def, 400, 200);
    verify_golden_or_create(fb, "gradient_repeat");
}

// ═══════════════════════════════════════════════════════════════════════
//  Golden 11: Spread Reflect (manual framebuffer — GradientDefinition)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("GradientGolden: spread Reflect (GradientDefinition)") {
    auto def = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::from_hex("#7c3aed")},
            {0.5f, Color::from_hex("#fbbf24")},
            {1.0f, Color::from_hex("#7c3aed")},
        },
        graphics::GradientSpread::Reflect);

    Framebuffer fb = render_gradient_definition_to_fb(def, 400, 200);
    verify_golden_or_create(fb, "gradient_reflect");
}

// ═══════════════════════════════════════════════════════════════════════
//  Integration: Six-card composite scene (DoD requirement)
//  Six rounded rectangles with different gradients, opacity, rotation,
//  and clipping on 1920x1080 neutral grey background.
// ═══════════════════════════════════════════════════════════════════════

Composition make_composite_scene() {
    return Composition({.width = 1920, .height = 1080, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });

            // Card 1: top-left, horizontal blue gradient
            s.layer("card1", [](LayerBuilder& l) {
                l.position({-550.0f, -280.0f, 0.0f});
                l.rounded_rect("r1", {
                    .size   = {400.0f, 220.0f},
                    .radius = 20.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {{0.0f, Color::from_hex("#1e3a5f")}, {1.0f, Color::from_hex("#3b82f6")}}),
                });
            });

            // Card 2: top-center, vertical green gradient with rotation
            s.layer("card2", [](LayerBuilder& l) {
                l.position({0.0f, -280.0f, 0.0f});
                l.rotate({0.0f, 0.0f, -10.0f});
                l.rounded_rect("r2", {
                    .size   = {400.0f, 220.0f},
                    .radius = 20.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.5f, 0.0f}, {0.5f, 1.0f},
                        {{0.0f, Color::from_hex("#065f46")}, {1.0f, Color::from_hex("#34d399")}}),
                });
            });

            // Card 3: top-right, diagonal purple gradient + opacity
            s.layer("card3", [](LayerBuilder& l) {
                l.position({550.0f, -280.0f, 0.0f});
                l.opacity(0.75f);
                l.rounded_rect("r3", {
                    .size   = {400.0f, 220.0f},
                    .radius = 20.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.0f}, {1.0f, 1.0f},
                        {{0.0f, Color::from_hex("#7c3aed")}, {1.0f, Color::from_hex("#e879f9")}}),
                });
            });

            // Card 4: bottom-left, radial gradient
            s.layer("card4", [](LayerBuilder& l) {
                l.position({-550.0f, 280.0f, 0.0f});
                l.circle("c4", {
                    .radius = 150.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::radial(
                        {0.5f, 0.5f}, 0.5f,
                        {{0.0f, Color::from_hex("#fef08a")}, {1.0f, Color::from_hex("#b45309")}}),
                });
            });

            // Card 5: bottom-center, multi-stop rainbow
            s.layer("card5", [](LayerBuilder& l) {
                l.position({0.0f, 280.0f, 0.0f});
                l.rounded_rect("r5", {
                    .size   = {450.0f, 200.0f},
                    .radius = 16.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#ef4444")},
                            {0.25f, Color::from_hex("#f59e0b")},
                            {0.5f, Color::from_hex("#10b981")},
                            {0.75f, Color::from_hex("#3b82f6")},
                            {1.0f, Color::from_hex("#8b5cf6")},
                        }),
                });
            });

            // Card 6: bottom-right, clipped (partially off-screen)
            s.layer("card6", [](LayerBuilder& l) {
                l.position({700.0f, 280.0f, 0.0f});
                l.rotate({0.0f, 0.0f, 15.0f});
                l.rounded_rect("r6", {
                    .size   = {500.0f, 280.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {{0.0f, Color::from_hex("#831843")}, {1.0f, Color::from_hex("#f472b6")}}),
                });
            });

            return s.build();
        });
}

TEST_CASE("GradientGolden: composite six-card scene") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_composite_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_golden_or_create(*rendered, "gradient_composite_scene");
}

// ═══════════════════════════════════════════════════════════════════════
//  Animation: three-frame animated gradient (frame 0, 15, 30)
// ═══════════════════════════════════════════════════════════════════════

Composition make_animated_gradient() {
    return Composition({.width = 960, .height = 540, .duration = 30},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.12f, 0.13f, 0.15f, 1.0f});
            });

            // Animate the gradient's "to" position to create a sweeping effect
            f32 t = static_cast<f32>(ctx.frame) / 30.0f;
            f32 sweep = t;  // 0 → 1 over the animation

            s.layer("animated_card", [sweep](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {700.0f, 300.0f},
                    .radius = 24.0f,
                    .color  = Color::white(),
                    .pos    = {0.0f, 0.0f, 0.0f},
                    .fill   = Fill::linear(
                        {sweep * 0.8f, 0.5f}, {sweep * 0.8f + 0.4f, 0.5f},
                        {
                            {0.0f, Color::from_hex("#1e3a5f")},
                            {0.5f, Color::from_hex("#f59e0b")},
                            {1.0f, Color::from_hex("#3b82f6")},
                        }),
                });
            });
            return s.build();
        });
}

TEST_CASE("GradientGolden: animated three-frame") {
    auto renderer = make_renderer();
    const auto comp = make_animated_gradient();

    auto fb0  = renderer.render_frame(comp, 0);
    auto fb15 = renderer.render_frame(comp, 15);
    auto fb29 = renderer.render_frame(comp, 29);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb15 != nullptr);
    REQUIRE(fb29 != nullptr);

    verify_golden_or_create(*fb0,  "gradient_animated_frame_00");
    verify_golden_or_create(*fb15, "gradient_animated_frame_15");
    verify_golden_or_create(*fb29, "gradient_animated_frame_29");

    // Frames must differ from each other
    CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb15));
    CHECK(framebuffer_hash(*fb15) != framebuffer_hash(*fb29));
}

}  // namespace
