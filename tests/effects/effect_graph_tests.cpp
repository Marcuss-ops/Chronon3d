#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/chronon3d.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <xxhash.h>

using namespace chronon3d;

namespace {

std::unique_ptr<Framebuffer> render_with_effects(
    std::function<void(LayerBuilder&)> effect_fn,
    bool modular = true
) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = modular;
    renderer.set_settings(settings);

    Composition comp({
        .name = "EffectTest",
        .width = 100,
        .height = 100,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("test_layer", [=](LayerBuilder& l) {
            effect_fn(l);
            l.rect("white_rect", {
                .size = {40, 40},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    return renderer.render_frame(comp, 0);
}

u64 fb_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

} // namespace

TEST_CASE("Test 10.1 — Blur effect blurs the pixels") {
    auto no_blur = render_with_effects([](LayerBuilder&) {});
    auto blurred = render_with_effects([](LayerBuilder& l) {
        l.blur(10.0f);
    });

    REQUIRE(no_blur != nullptr);
    REQUIRE(blurred != nullptr);

    // Without blur, the boundary at x=20, y=50 is sharp (either white 1.0 or black 0.0).
    // With blur, the edge must be smoothed out, so pixels at (20, 50) have intermediate alpha.
    Color p_nob = no_blur->get_pixel(28, 50); // Outside 40x40 box (offset +/-20 around center 50)
    Color p_bl  = blurred->get_pixel(28, 50);

    CHECK(p_nob.a == 0.0f);
    CHECK(p_bl.a > 0.0f);
    CHECK(p_bl.a < 1.0f);
}

TEST_CASE("Test 10.2 — Tint effect shifts pixel colors") {
    auto tinted = render_with_effects([](LayerBuilder& l) {
        l.tint(Color::red(), 1.0f);
    });

    REQUIRE(tinted != nullptr);
    Color center = tinted->get_pixel(50, 50);
    // Center was white (1.0, 1.0, 1.0), now tinted fully to red
    CHECK(center.r > 0.8f);
    CHECK(center.g < 0.2f);
    CHECK(center.b < 0.2f);
}

TEST_CASE("Test 10.3 — Brightness effect scales pixel values") {
    auto brightened = render_with_effects([](LayerBuilder& l) {
        l.tint(Color{0.5f, 0.5f, 0.5f, 1.0f}, 1.0f);
        l.brightness(0.3f);
    });

    REQUIRE(brightened != nullptr);
    Color center = brightened->get_pixel(50, 50);
    // 0.5 + 0.3 = 0.8
    CHECK(center.r > 0.7f);
}

TEST_CASE("Test 10.4 — Contrast effect adjusts visual contrast") {
    auto standard = render_with_effects([](LayerBuilder& l) {
        // use dark-gray rect (0.2)
        l.tint(Color{0.2f, 0.2f, 0.2f, 1.0f}, 1.0f);
    });
    auto high_contrast = render_with_effects([](LayerBuilder& l) {
        l.tint(Color{0.2f, 0.2f, 0.2f, 1.0f}, 1.0f);
        l.contrast(1.5f);
    });

    REQUIRE(standard != nullptr);
    REQUIRE(high_contrast != nullptr);

    Color center_std = standard->get_pixel(50, 50);
    Color center_hc  = high_contrast->get_pixel(50, 50);

    CHECK(center_std.r == doctest::Approx(0.2f).epsilon(0.05f));
    // High contrast pushes values away from the midpoint (0.5). 0.2 is < 0.5, so it should get DARKER.
    CHECK(center_hc.r < center_std.r);
}

TEST_CASE("Test 10.5 — Glow effect adds visual glowing border") {
    auto glowed = render_with_effects([](LayerBuilder& l) {
        l.glow(12.0f, 0.8f, Color::white());
    });

    REQUIRE(glowed != nullptr);
    // Without glow, pixel at (25, 50) is empty. With glow, it has intensity.
    Color edge_glow = glowed->get_pixel(25, 50);
    CHECK(edge_glow.a > 0.0f);
}

TEST_CASE("Test 10.6 — Drop shadow renders offset shadow pixels") {
    auto shadowed = render_with_effects([](LayerBuilder& l) {
        l.drop_shadow({15.0f, 15.0f}, Color::black(), 5.0f);
    });

    REQUIRE(shadowed != nullptr);
    // Rect center is at (50, 50), rect bounds = 30 to 70.
    // Shadow is offset by (15, 15) to the right-bottom.
    // Pixel at (80, 80) should have shadow pixels (translucent black).
    Color shadow_pix = shadowed->get_pixel(80, 80);
    CHECK(shadow_pix.a > 0.0f);
    CHECK(shadow_pix.r < 0.2f);
}

TEST_CASE("Test 10.7 — Disabled effects do not affect hash or rendering") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp_no_effect({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("test_layer", [](LayerBuilder& l) {
            l.rect("white_rect", {.size = {40, 40}, .color = Color::white()});
        });
        return s.build();
    });

    Composition comp_disabled_effect({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        LayerBuilder builder("test_layer", ctx.resource);
        builder.blur(10.0f);
        builder.rect("white_rect", {.size = {40, 40}, .color = Color::white()});

        Layer l = builder.build();
        // Mutate effects to disable
        l.effects[0].enabled = false;

        Scene scene(ctx.resource);
        scene.add_layer(std::move(l));
        return scene;
    });

    auto fb1 = renderer.render_frame(comp_no_effect, 0);
    auto fb2 = renderer.render_frame(comp_disabled_effect, 0);

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb_hash(*fb1) == fb_hash(*fb2));
}

TEST_CASE("Test 10.8 — Effect execution order affects output result") {
    // Blur then Tint vs Tint then Blur yields different pixel layouts.
    auto blur_then_tint = render_with_effects([](LayerBuilder& l) {
        l.blur(10.0f);
        l.tint(Color::red(), 1.0f);
    });

    auto tint_then_blur = render_with_effects([](LayerBuilder& l) {
        l.tint(Color::red(), 1.0f);
        l.blur(10.0f);
    });

    REQUIRE(blur_then_tint != nullptr);
    REQUIRE(tint_then_blur != nullptr);
    CHECK(fb_hash(*blur_then_tint) != fb_hash(*tint_then_blur));
}
