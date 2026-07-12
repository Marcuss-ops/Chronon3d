#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/golden_test.hpp>
#include <filesystem>
#include <cmath>
#include <chronon3d/text/text_placement.hpp>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

// colors_near is preserved (NOT a candidate for removal as dead code) because
// Test 17.3 still uses it for intentional-mismatch diff image highlighting
// (the in-memory fake_golden + diff.set_pixel highlight loop). The 6 tests
// migrated to the canonical verify_golden() mechanism in this commit do NOT
// use it anymore — verify_golden() handles pixel comparison internally via
// compare_framebuffers() (5-metric ImageDiffThreshold).
bool colors_near(const Color& c1, const Color& c2, float tolerance = 0.05f) {
    return std::abs(c1.r - c2.r) <= tolerance &&
           std::abs(c1.g - c2.g) <= tolerance &&
           std::abs(c1.b - c2.b) <= tolerance &&
           std::abs(c1.a - c2.a) <= tolerance;
}

// Canonical GoldenTestConfig for the Tests 17.1-17.8 surface.
// Threshold values mirror tests/text_golden/user_spec/01_text_basic_centered.cpp
// (text rendering can have minor pixel-level variations across host/compiler).
GoldenTestConfig make_golden_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden";
    cfg.artifact_directory = "test_renders/artifacts/golden";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

} // namespace

TEST_CASE("Test 17.1 — Golden image baseline and pixel-by-pixel validation") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 64, .height = 64}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("red_box", {.size={30, 30}, .color=Color::red(), .pos={32, 32, 0}});
        s.circle("blue_dot", {.radius=8.0f, .color=Color::blue(), .pos={32, 32, 0}});
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    auto result = verify_golden(*rendered, "shapes_golden", make_golden_config());
    REQUIRE_GOLDEN_PASSED(result);
}

TEST_CASE("Test 17.2 — Framebuffer dimension and float boundary comparisons") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 128, .height = 64}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("box", {.size={60, 30}, .color=Color::white(), .pos={0, 0, 0}});
        return s.build();
    });

    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 128);
    CHECK(fb->height() == 64);

    // Float checks on specific pixel offsets (interpolation values near boundaries)
    Color boundary_left = fb->get_pixel(34, 32);
    Color boundary_right = fb->get_pixel(93, 32);

    CHECK(boundary_left.r > 0.9f);
    CHECK(boundary_right.r > 0.9f);
}

TEST_CASE("Test 17.3 — Pixel-level difference reporting on mismatch") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 32, .height = 32}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("green_box", {.size={20, 20}, .color=Color::green(), .pos={16, 16, 0}});
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    // We intentionally create a mismatched fake "golden" image (completely black)
    Framebuffer fake_golden(32, 32);
    fake_golden.clear(Color::black());

    // Generate diff image highlighting mismatches
    Framebuffer diff(32, 32);
    diff.clear(Color::transparent());

    bool match = true;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            Color r_pixel = rendered->get_pixel(x, y);
            Color g_pixel = fake_golden.get_pixel(x, y);
            if (!colors_near(r_pixel, g_pixel)) {
                match = false;
                // Highlight different pixel in red
                diff.set_pixel(x, y, Color::red());
            } else {
                diff.set_pixel(x, y, Color{0, 0, 0, 0.2f}); // match shade
            }
        }
    }

    CHECK_FALSE(match); // Should fail comparison as expected

    // Write difference report image to disk
    const std::filesystem::path diff_dir = "output/debug";
    std::filesystem::create_directories(diff_dir);
    const std::filesystem::path diff_path = diff_dir / "diff_shapes.png";
    std::filesystem::remove(diff_path);

    REQUIRE(save_png(diff, diff_path.string()));
    CHECK(std::filesystem::exists(diff_path));
}

TEST_CASE("Test 17.4 — Text layout alignment Center/Middle") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 256, .height = 256}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t1", {
                .content = {.value = "Centered Middle"},
                .placement = TextPlacement{TextPlacementKind::Absolute, {128.0f, 128.0f}},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 24.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color::white()},
            });
        });
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    auto result = verify_golden(*rendered, "text_align_golden", make_golden_config());
    REQUIRE_GOLDEN_PASSED(result);
}

TEST_CASE("Test 17.5 — Text auto-fit automatic sizing") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t2", {
                .content = {.value = "This is a very long title that needs to fit inside a small box automatically without overflowing"},
                .placement = TextPlacement{TextPlacementKind::Absolute, {128.0f, 64.0f}},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 48.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle,
                           .auto_fit = true,
                           .min_font_size = 8.0f,
                           .max_font_size = 48.0f},
                .appearance = {.color = Color::white()},
            });
        });
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    auto result = verify_golden(*rendered, "text_autofit_golden", make_golden_config());
    REQUIRE_GOLDEN_PASSED(result);
}

TEST_CASE("Test 17.6 — Text max-lines and ellipsis truncation") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t3", {
                .content = {.value = "Line One Wordy\nLine Two Wordy\nLine Three Wordy\nLine Four Wordy"},
                .placement = TextPlacement{TextPlacementKind::Absolute, {128.0f, 64.0f}},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 20.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle,
                           .max_lines = 2,
                           .ellipsis = true},
                .appearance = {.color = Color::white()},
            });
        });
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    auto result = verify_golden(*rendered, "text_ellipsis_golden", make_golden_config());
    REQUIRE_GOLDEN_PASSED(result);
}

TEST_CASE("Test 17.7 — Text style (Cyan neon-like coloring)") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t4", {
                .content = {.value = "CYAN NEON"},
                .placement = TextPlacement{TextPlacementKind::Absolute, {128.0f, 64.0f}},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 28.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.0f, 1.0f, 0.8f, 1.0f}}, // Cyan
            });
        });
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    auto result = verify_golden(*rendered, "text_cyan_neon_golden", make_golden_config());
    REQUIRE_GOLDEN_PASSED(result);
}

TEST_CASE("Test 17.8 — Subtitle backing box rendering") {
    auto renderer = test::make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t5", {
                .content = {.value = "Subtitle Box"},
                .placement = TextPlacement{TextPlacementKind::Absolute, {128.0f, 64.0f}},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 22.0f},
                .layout = {.box = {240.0f, 80.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.9f, 0.9f, 0.9f, 1.0f},
                               .box_style = {.enabled = true,
                                             .padding = {16.0f, 8.0f},
                                             .radius = 8.0f,
                                             .background = Color{0.0f, 0.0f, 0.0f, 0.65f}}},
            });
        });
        return s.build();
    });

    auto rendered = renderer.render(comp, 0);
    REQUIRE(rendered != nullptr);

    auto result = verify_golden(*rendered, "text_box_golden", make_golden_config());
    REQUIRE_GOLDEN_PASSED(result);
}
