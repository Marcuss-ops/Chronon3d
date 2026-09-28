#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


namespace {

std::shared_ptr<Framebuffer> render_masked_layer(
    std::function<void(LayerBuilder&)> mask_fn
) {
    auto renderer = test::make_renderer();
    RenderSettings settings;
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

    return renderer.render(comp, 0);
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

TEST_CASE("LayerBuilder exposes ellipse mask geometry") {
    const auto scene = SceneBuilder{}.layer("ellipse_mask", [](LayerBuilder& layer) {
        layer.mask_ellipse({.size = {80.0f, 40.0f}, .pos = {3.0f, -2.0f, 0.0f},
                            .feather = 4.0f, .expansion = 1.0f, .inverted = true});
    }).build();
    REQUIRE(scene.layers().size() == 1);
    const auto& mask = scene.layers()[0].mask;
    CHECK(mask.type == MaskType::Ellipse);
    CHECK(mask.size.x == doctest::Approx(80.0f));
    CHECK(mask.size.y == doctest::Approx(40.0f));
    CHECK(mask.pos.x == doctest::Approx(3.0f));
    CHECK(mask.inverted);
}

TEST_CASE("inverted mask rasterization retains coverage outside source bounds") {
    Mask mask;
    mask.type = MaskType::Circle;
    mask.pos = {16.0f, 16.0f, 0.0f};
    mask.radius = 5.0f;
    mask.inverted = true;

    const auto alpha = rasterize_mask_alpha(mask, Mat4{1.0f}, 32, 32);
    REQUIRE(alpha);
    CHECK(alpha->get_pixel(0, 0).a == doctest::Approx(1.0f));
    CHECK(alpha->get_pixel(16, 16).a == doctest::Approx(0.0f));
}

TEST_CASE("Path mask supports compound closed geometry, feather, and expansion") {
    Mask mask;
    mask.type = MaskType::Path;
    mask.path = {PathCommand::move_to({-10, -10}), PathCommand::line_to({10, -10}),
                 PathCommand::line_to({10, 10}), PathCommand::line_to({-10, 10}),
                 PathCommand::close()};
    mask.feather = 4.0f;
    mask.expansion = 2.0f;
    CHECK(mask_alpha_at_local_point(mask, {0, 0}) == doctest::Approx(1.0f));
    CHECK(mask_alpha_at_local_point(mask, {12, 0}) == doctest::Approx(0.5f));
    CHECK(mask_alpha_at_local_point(mask, {14, 0}) == doctest::Approx(0.0f));
}

TEST_CASE("Primitive masks apply feather and expansion through alpha coverage") {
    Mask mask;
    mask.type = MaskType::Rect;
    mask.size = {20.0f, 20.0f};
    mask.feather = 4.0f;
    mask.expansion = 2.0f;
    CHECK(mask_alpha_at_local_point(mask, {0, 0}) == doctest::Approx(1.0f));
    CHECK(mask_alpha_at_local_point(mask, {12, 0}) == doctest::Approx(0.5f));
    CHECK(mask_alpha_at_local_point(mask, {14, 0}) == doctest::Approx(0.0f));

    mask.type = MaskType::Ellipse;
    mask.size = {20.0f, 12.0f};
    mask.expansion = 0.0f;
    mask.feather = 4.0f;
    CHECK(mask_alpha_at_local_point(mask, {10, 0}) == doctest::Approx(0.5f));
}

TEST_CASE("Mask opacity scales primitive and inverted coverage") {
    Mask mask;
    mask.type = MaskType::Rect;
    mask.size = {20.0f, 20.0f};
    mask.opacity = 0.25f;
    CHECK(mask_alpha_at_local_point(mask, {0, 0}) == doctest::Approx(0.25f));
    mask.inverted = true;
    CHECK(mask_alpha_at_local_point(mask, {0, 0}) == doctest::Approx(0.0f));
    CHECK(mask_alpha_at_local_point(mask, {20, 0}) == doctest::Approx(0.25f));
}

TEST_CASE("Path mask clips rendered layer pixels") {
    auto fb = render_masked_layer([](LayerBuilder& layer) {
        layer.mask_path({.commands = {PathCommand::move_to({-10, -10}),
                                      PathCommand::line_to({10, -10}),
                                      PathCommand::line_to({10, 10}),
                                      PathCommand::line_to({-10, 10}),
                                      PathCommand::close()}});
    });
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).a > 0.9f);
    CHECK(fb->get_pixel(70, 50).a == 0.0f);
}

TEST_CASE("LayerBuilder stores and recenters path mask geometry") {
    auto scene = SceneBuilder{}.layer("path_mask", [](LayerBuilder& layer) {
        layer.mask_path({.commands = {PathCommand::move_to({10, 20}),
                                      PathCommand::line_to({30, 20}),
                                      PathCommand::line_to({30, 40}),
                                      PathCommand::close()},
                          .feather = 2.0f, .expansion = 1.0f});
    }).build();
    const auto& mask = scene.layers()[0].mask;
    REQUIRE(mask.type == MaskType::Path);
    CHECK(mask.size.x == doctest::Approx(20.0f));
    CHECK(mask.pos.x == doctest::Approx(20.0f));
    CHECK(mask.pos.y == doctest::Approx(30.0f));
    CHECK(mask.path[0].p0.x == doctest::Approx(-10.0f));
}
