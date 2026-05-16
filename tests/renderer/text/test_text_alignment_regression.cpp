#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <filesystem>
#include <limits>

using namespace chronon3d;

namespace {

struct PixelBounds {
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    bool found;
};

static PixelBounds find_non_black_bounds(const Framebuffer& fb, float threshold = 0.02f) {
    PixelBounds b{
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::min(),
        false
    };

    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const auto p = fb.get_pixel(x, y);
            if (p.r > threshold || p.g > threshold || p.b > threshold) {
                b.found = true;
                b.min_x = std::min(b.min_x, x);
                b.min_y = std::min(b.min_y, y);
                b.max_x = std::max(b.max_x, x);
                b.max_y = std::max(b.max_y, y);
            }
        }
    }

    return b;
}

static float center_y(const PixelBounds& b) {
    return static_cast<float>(b.min_y + b.max_y) * 0.5f;
}

static float sum_channel_b(const Framebuffer& fb) {
    float sum = 0.0f;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            sum += fb.get_pixel(x, y).b;
        }
    }
    return sum;
}

}

TEST_CASE("Text direct renderer and scene path have matching vertical placement") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";

    if (!std::filesystem::exists(font)) {
        return;
    }

    constexpr int W = 360;
    constexpr int H = 180;
    constexpr float X = 180.0f;
    constexpr float Y = 90.0f;

    TextShape text;
    text.text = "TEST";
    text.style.font_path = font;
    text.style.size = 64.0f;
    text.style.color = Color{1, 1, 1, 1};
    text.style.align = TextAlign::Center;

    Framebuffer direct_fb(W, H);
    direct_fb.clear(Color{0, 0, 0, 1});

    TextRenderer direct_renderer;
    // Note: SoftwareRenderer already uses STB font backend by default in this environment
    // but we ensure consistency here.

    Transform direct_tr;
    direct_tr.position = {X, Y, 0};
    
    RenderState direct_state;
    direct_state.matrix = math::translate(direct_tr.position);

    REQUIRE(direct_renderer.draw_text(text, direct_tr, direct_fb, &direct_state));

    SceneBuilder s;
    s.layer("text_layer", [&](LayerBuilder& l) {
        l.text("text", {
            .content = "TEST",
            .style = text.style
        }).at({X, Y, 0});
    });

    auto scene = s.build();

    SoftwareRenderer scene_renderer;
    Camera camera;
    auto scene_fb = scene_renderer.render_scene(scene, camera, W, H);

    REQUIRE(scene_fb != nullptr);

    const auto direct_bounds = find_non_black_bounds(direct_fb);
    const auto scene_bounds = find_non_black_bounds(*scene_fb);

    REQUIRE(direct_bounds.found);
    REQUIRE(scene_bounds.found);

    // Verify centers match within a small epsilon (10% of font size)
    CHECK(center_y(scene_bounds) == doctest::Approx(center_y(direct_bounds)).epsilon(0.1));
}

TEST_CASE("Text glow creates visible pixels outside normal text bounds") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";

    if (!std::filesystem::exists(font)) {
        return;
    }

    auto render = [&](bool glow_enabled) {
        SceneBuilder s;
        s.layer("glow_layer", [&](LayerBuilder& l) {
            l.text("text", {
                .content = "TEST",
                .style = {
                    .font_path = font,
                    .size = 64.0f,
                    .color = Color{1, 1, 1, 1},
                    .align = TextAlign::Center
                }
            }).at({180, 90, 0});

            if (glow_enabled) {
                l.with_glow({
                    .enabled = true,
                    .radius = 18.0f,
                    .intensity = 1.0f,
                    .color = Color{0.0f, 0.4f, 1.0f, 1.0f}
                });
            }
        });

        SoftwareRenderer renderer;
        Camera camera;
        auto fb = renderer.render_scene(s.build(), camera, 360, 180);
        REQUIRE(fb != nullptr);
        return fb;
    };

    auto normal_fb = render(false);
    auto glow_fb = render(true);

    const auto normal_bounds = find_non_black_bounds(*normal_fb, 0.05f);
    REQUIRE(normal_bounds.found);

    float outside_blue = 0.0f;
    int pixels_outside = 0;

    for (int y = 0; y < glow_fb->height(); ++y) {
        for (int x = 0; x < glow_fb->width(); ++x) {
            const bool outside_normal_text =
                x < normal_bounds.min_x - 5 ||
                x > normal_bounds.max_x + 5 ||
                y < normal_bounds.min_y - 5 ||
                y > normal_bounds.max_y + 5;

            if (outside_normal_text) {
                float b = glow_fb->get_pixel(x, y).b;
                if (b > 0.01f) {
                    outside_blue += b;
                    pixels_outside++;
                }
            }
        }
    }

    // Glow must produce a significant amount of light outside the text glyphs
    CHECK(pixels_outside > 50);
    CHECK(outside_blue > 5.0f);
}

TEST_CASE("Text glow intensity scales linearly, not quadratically") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) return;

    auto render_glow = [&](float intensity) {
        SceneBuilder s;
        s.layer("glow_layer", [&](LayerBuilder& l) {
            l.text("text", {
                .content = "GLOW",
                .style = {
                    .font_path = font,
                    .size = 64.0f,
                    .color = Color{0, 0, 0, 1}, // Black text to isolate glow
                    .align = TextAlign::Center
                }
            }).at({180, 90, 0}).with_glow({
                .enabled = true,
                .radius = 16.0f,
                .intensity = intensity,
                .color = Color{0.0f, 0.5f, 1.0f, 1.0f}
            });
        });

        SoftwareRenderer renderer;
        Camera camera;
        auto fb = renderer.render_scene(s.build(), camera, 360, 180);
        REQUIRE(fb != nullptr);
        return fb;
    };

    auto full = render_glow(1.0f);
    auto half = render_glow(0.5f);

    const float full_blue = sum_channel_b(*full);
    const float half_blue = sum_channel_b(*half);

    REQUIRE(full_blue > 0.1f);
    const float ratio = half_blue / full_blue;

    // With linear scaling, ratio should be around 0.5.
    // If it was double-premultiplied (alpha * intensity * intensity), it would be 0.25 or less.
    CHECK(ratio > 0.4f);
    CHECK(ratio < 0.6f);
}
