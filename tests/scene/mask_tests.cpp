#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

namespace {

std::unique_ptr<Framebuffer> render_masked_layer(
    std::function<void(LayerBuilder&)> mask_fn
) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp({
        .name = "MaskTest",
        .width = 100,
        .height = 100,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("test_layer", [=](LayerBuilder& l) {
            mask_fn(l);
            l.rect("white_rect", {
                .size = {80, 80},
                .color = Color::white(),
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    return renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Test 12.1 — Rect mask restricts rendering boundary") {
    auto fb = render_masked_layer([](LayerBuilder& l) {
        l.mask_rect({.size = {30, 30}, .pos = {0, 0, 0}, .inverted = false});
    });

    REQUIRE(fb != nullptr);
    // Center at (50,50) is inside mask (30x30 around center is 35 to 65).
    // Pixel at (50, 50) must be white.
    // Pixel at (30, 50) is outside the 30x30 mask but inside the 80x80 rect, so it must be masked (alpha = 0).
    CHECK(fb->get_pixel(50, 50).a > 0.9f);
    CHECK(fb->get_pixel(30, 50).a == 0.0f);
}

TEST_CASE("Test 12.2 — Rounded rect mask rounds the mask corners") {
    auto fb = render_masked_layer([](LayerBuilder& l) {
        l.mask_rounded_rect({.size = {40, 40}, .radius = 10.0f, .pos = {0, 0, 0}, .inverted = false});
    });

    REQUIRE(fb != nullptr);
    // Inside center is white
    CHECK(fb->get_pixel(50, 50).a > 0.9f);
    // Near corner of 40x40 mask (offset by 20 from center: 30, 30) should be rounded away (so alpha is 0)
    CHECK(fb->get_pixel(30, 30).a == 0.0f);
}

TEST_CASE("Test 12.3 — Circle mask restricts rendering inside radius") {
    auto fb = render_masked_layer([](LayerBuilder& l) {
        l.mask_circle({.radius = 20.0f, .pos = {0, 0, 0}, .inverted = false});
    });

    REQUIRE(fb != nullptr);
    // Inside radius (distance < 20 from center (50,50)) is white
    CHECK(fb->get_pixel(50, 50).a > 0.9f);
    CHECK(fb->get_pixel(65, 50).a > 0.9f); // dist = 15 < 20
    // Outside radius (distance > 20) is transparent
    CHECK(fb->get_pixel(75, 50).a == 0.0f); // dist = 25 > 20
}

TEST_CASE("Test 12.4 — Inverted mask punches out center pixels") {
    auto fb = render_masked_layer([](LayerBuilder& l) {
        l.mask_circle({.radius = 20.0f, .pos = {0, 0, 0}, .inverted = true});
    });

    REQUIRE(fb != nullptr);
    // Inverted mask: inside radius is transparent (punched out)
    CHECK(fb->get_pixel(50, 50).a == 0.0f);
    // Outside radius but inside 80x80 rect is white
    CHECK(fb->get_pixel(20, 50).a > 0.9f);
}

TEST_CASE("Test 12.5 — Local mask positioning offsets the mask area") {
    auto fb = render_masked_layer([](LayerBuilder& l) {
        // Shift mask to the right by 20 pixels
        l.mask_circle({.radius = 15.0f, .pos = {20, 0, 0}, .inverted = false});
    });

    REQUIRE(fb != nullptr);
    // Mask center is now at (70, 50)
    CHECK(fb->get_pixel(70, 50).a > 0.9f);
    // Original center (50, 50) is 20px away, which is outside radius 15, so transparent
    CHECK(fb->get_pixel(50, 50).a == 0.0f);
}

TEST_CASE("Test 12.6 — LayerBuilder sets mask struct fields correctly") {
    auto scene = SceneBuilder{}
        .layer("test", [](LayerBuilder& l) {
            l.mask_circle({.radius = 35.0f, .pos = {5, 10, 0}, .inverted = true});
        })
        .build();

    REQUIRE(scene.layers().size() == 1);
    const auto& layer = scene.layers()[0];
    REQUIRE(layer.mask.enabled());
    CHECK(layer.mask.type == MaskType::Circle);
    CHECK(layer.mask.radius == 35.0f);
    CHECK(layer.mask.pos.x == 5.0f);
    CHECK(layer.mask.pos.y == 10.0f);
    CHECK(layer.mask.inverted == true);
}
