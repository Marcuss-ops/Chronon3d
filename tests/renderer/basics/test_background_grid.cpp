#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>

#include <string>

using namespace chronon3d;

TEST_CASE("Background grid is rendered directly from core APIs") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.name = "BackgroundGrid", .width = 100, .height = 100, .duration = 1};

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx.resource);

        const Vec3 center{static_cast<f32>(ctx.width) * 0.5f, static_cast<f32>(ctx.height) * 0.5f, 0.0f};
        const Vec2 size{static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)};
        const Color background = Color::from_hex("#111111");
        const Color grid = Color{1.0f, 1.0f, 1.0f, 0.15f};

        scene.rect("background", {.size = size, .color = background, .pos = center});

        constexpr i32 step = 20;

        for (i32 x = step; x < ctx.width; x += step) {
            scene.line("grid-v-" + std::to_string(x),
                       {static_cast<f32>(x), 0.0f, 0.0f},
                       {static_cast<f32>(x), static_cast<f32>(ctx.height - 1), 0.0f},
                       grid);
        }

        for (i32 y = step; y < ctx.height; y += step) {
            scene.line("grid-h-" + std::to_string(y),
                       {0.0f, static_cast<f32>(y), 0.0f},
                       {static_cast<f32>(ctx.width - 1), static_cast<f32>(y), 0.0f},
                       grid);
        }

        return scene.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 100);
    CHECK(fb->height() == 100);

    const Color background = Color::from_hex("#111111");
    const Color grid = Color{1.0f, 1.0f, 1.0f, 0.15f};

    const Color off_grid = fb->get_pixel(10, 10);
    CHECK(off_grid.r >= 0.0f);
    CHECK(off_grid.g >= 0.0f);
    CHECK(off_grid.b >= 0.0f);
    CHECK(off_grid.r < 0.02f);
    CHECK(off_grid.g < 0.02f);
    CHECK(off_grid.b < 0.02f);

    const Color on_vertical = fb->get_pixel(20, 10);
    const Color on_horizontal = fb->get_pixel(10, 20);
    CHECK(on_vertical.r > off_grid.r);
    CHECK(on_vertical.g > off_grid.g);
    CHECK(on_vertical.b > off_grid.b);
    CHECK(on_horizontal.r > off_grid.r);
    CHECK(on_horizontal.g > off_grid.g);
    CHECK(on_horizontal.b > off_grid.b);

    CHECK(on_vertical.r > 0.1f);
    CHECK(on_horizontal.r > 0.1f);
}
