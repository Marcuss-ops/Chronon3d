#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

void draw_dark_grid_background(SceneBuilder& s, i32 width, i32 height) {
    const f32 W = static_cast<f32>(width);
    const f32 H = static_cast<f32>(height);
    const f32 spacing = 80.0f;
    const f32 major_step = spacing * 4.0f;
    const Color minor{1.0f, 1.0f, 1.0f, 0.08f};
    Color major = minor;
    major.a = 0.20f;

    s.rect("fill", {
        .size = {W, H},
        .color = Color{0.058f, 0.058f, 0.065f, 1.0f},
        .pos = {0.0f, 0.0f, 0.0f},
    });

    const f32 half_w = W * 0.5f;
    const f32 half_h = H * 0.5f;

    for (f32 x = -half_w; x <= half_w; x += spacing) {
        s.line("vx", {.from = {x, -half_h, 0.0f}, .to = {x, half_h, 0.0f}, .thickness = 1.5f, .color = minor});
    }
    for (f32 y = -half_h; y <= half_h; y += spacing) {
        s.line("hy", {.from = {-half_w, y, 0.0f}, .to = {half_w, y, 0.0f}, .thickness = 1.5f, .color = minor});
    }
    for (f32 x = -half_w; x <= half_w; x += major_step) {
        s.line("Vx", {.from = {x, -half_h, 0.0f}, .to = {x, half_h, 0.0f}, .thickness = 3.0f, .color = major});
    }
    for (f32 y = -half_h; y <= half_h; y += major_step) {
        s.line("Hy", {.from = {-half_w, y, 0.0f}, .to = {half_w, y, 0.0f}, .thickness = 3.0f, .color = major});
    }

    s.layer("spotlight", [W, H](LayerBuilder& l) {
        l.circle("spot", {
            .radius = W * 0.40f,
            .color = Color{1.0f, 1.0f, 1.0f, 0.028f},
            .pos = {0.0f, -H * 0.22f, 0.0f},
        }).blur(W * 0.14f);
    });
}

Composition make_lil_dirk_reference() {
    return composition({
        .name = "LilDirkReference",
        .width = 1920,
        .height = 1080,
        .duration = 1,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        draw_dark_grid_background(s, ctx.width, ctx.height);

        s.rounded_rect("banner_halo", {
            .size = {760.0f, 182.0f},
            .radius = 14.0f,
            .color = Color{1.0f, 1.0f, 1.0f, 0.18f},
            .pos = {30.0f, 6.0f, 0.0f},
        }).with_glow(Glow{
            .enabled = true,
            .radius = 14.0f,
            .intensity = 0.22f,
            .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
        });

        s.rounded_rect("top_bar", {
            .size = {700.0f, 54.0f},
            .radius = 4.0f,
            .color = Color{0.92f, 0.23f, 0.22f, 1.0f},
            .pos = {10.0f, -53.0f, 0.0f},
        });

        s.rounded_rect("bottom_bar", {
            .size = {700.0f, 54.0f},
            .radius = 4.0f,
            .color = Color{0.92f, 0.23f, 0.22f, 1.0f},
            .pos = {10.0f, 52.0f, 0.0f},
        });

        s.rect("center_band", {
            .size = {560.0f, 36.0f},
            .color = Color{0.10f, 0.10f, 0.10f, 0.58f},
            .pos = {10.0f, -18.0f, 0.0f},
        });

        s.text("title", {
            .content = "LIL DIRK",
            .style = {
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 118.0f,
                .color = Color::white(),
                .align = TextAlign::Center,
            },
            .pos = {0.0f, -30.0f, 0.0f},
        }).with_glow(Glow{
            .enabled = true,
            .radius = 18.0f,
            .intensity = 0.68f,
            .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("Lil Dirk reference frame renders banner text over a dark grid") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping Lil Dirk reference because font fixture is missing");
        return;
    }

    auto fb = render_modular(make_lil_dirk_reference());
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);

    const std::filesystem::path out = "output/debug/lil_dirk/frame_001.png";
    save_debug(*fb, out.string());

    bool found_red = false;
    bool found_white = false;
    for (int y = 360; y < 720 && (!found_red || !found_white); ++y) {
        for (int x = 280; x < 1640 && (!found_red || !found_white); ++x) {
            const Color c = fb->get_pixel(x, y);
            if (!found_red && c.r > 0.75f && c.g < 0.45f && c.b < 0.45f) {
                found_red = true;
            }
            if (!found_white && c.r > 0.9f && c.g > 0.9f && c.b > 0.9f) {
                found_white = true;
            }
        }
    }
    CHECK(found_red);
    CHECK(found_white);

    CHECK(std::filesystem::exists(out));
}
