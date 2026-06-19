#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <tests/helpers/test_utils.hpp>
#include <filesystem>
#include <cmath>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

bool colors_near(const Color& c1, const Color& c2, float tolerance = 0.05f) {
    return std::abs(c1.r - c2.r) <= tolerance &&
           std::abs(c1.g - c2.g) <= tolerance &&
           std::abs(c1.b - c2.b) <= tolerance &&
           std::abs(c1.a - c2.a) <= tolerance;
}

} // namespace

TEST_CASE("Test 17.1 — Golden image baseline and pixel-by-pixel validation") {
    auto renderer = make_renderer();
    Composition comp({.width = 64, .height = 64}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("red_box", {.size={30, 30}, .color=Color::red(), .pos={32, 32, 0}});
        s.circle("blue_dot", {.radius=8.0f, .color=Color::blue(), .pos={32, 32, 0}});
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "shapes_golden.png";

    // 1. Generate golden baseline if it does not exist
    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    // 2. Load golden image and compare pixel-by-pixel with tolerance
    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);
    REQUIRE(golden->width() == rendered->width());
    REQUIRE(golden->height() == rendered->height());

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}

TEST_CASE("Test 17.2 — Framebuffer dimension and float boundary comparisons") {
    auto renderer = make_renderer();
    Composition comp({.width = 128, .height = 64}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("box", {.size={60, 30}, .color=Color::white(), .pos={0, 0, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
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
    auto renderer = make_renderer();
    Composition comp({.width = 32, .height = 32}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("green_box", {.size={20, 20}, .color=Color::green(), .pos={16, 16, 0}});
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
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
    auto renderer = make_renderer();
    Composition comp({.width = 256, .height = 256}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t1", {
                .content = {.value = "Centered Middle"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 24.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color::white()},
                .position = {128.0f, 128.0f, 0.0f}
            });
        });
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "text_align_golden.png";

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);
    REQUIRE(golden->width() == rendered->width());
    REQUIRE(golden->height() == rendered->height());

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y).to_srgb(), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}

TEST_CASE("Test 17.5 — Text auto-fit automatic sizing") {
    auto renderer = make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t2", {
                .content = {.value = "This is a very long title that needs to fit inside a small box automatically without overflowing"},
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
                .position = {128.0f, 64.0f, 0.0f}
            });
        });
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "text_autofit_golden.png";

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y).to_srgb(), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}

TEST_CASE("Test 17.6 — Text max-lines and ellipsis truncation") {
    auto renderer = make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t3", {
                .content = {.value = "Line One Wordy\nLine Two Wordy\nLine Three Wordy\nLine Four Wordy"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 20.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle,
                           .max_lines = 2,
                           .ellipsis = true},
                .appearance = {.color = Color::white()},
                .position = {128.0f, 64.0f, 0.0f}
            });
        });
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "text_ellipsis_golden.png";

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y).to_srgb(), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}

TEST_CASE("Test 17.7 — Text style (Cyan neon-like coloring)") {
    auto renderer = make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t4", {
                .content = {.value = "CYAN NEON"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 28.0f},
                .layout = {.box = {240.0f, 100.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.0f, 1.0f, 0.8f, 1.0f}}, // Cyan
                .position = {128.0f, 64.0f, 0.0f}
            });
        });
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "text_cyan_neon_golden.png";

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y).to_srgb(), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}

TEST_CASE("Test 17.8 — Subtitle backing box rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 256, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text_layer", [](LayerBuilder& l) {
            l.text("t5", {
                .content = {.value = "Subtitle Box"},
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
                .position = {128.0f, 64.0f, 0.0f}
            });
        });
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "text_box_golden.png";

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y).to_srgb(), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}
