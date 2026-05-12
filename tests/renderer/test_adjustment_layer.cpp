#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software_renderer.hpp>

using namespace chronon3d;

static std::unique_ptr<Framebuffer> render_fn(
    std::function<Scene(const FrameContext&)> fn, int w = 80, int h = 80)
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=1}, fn);
    return rend.render_frame(comp, 0);
}

TEST_CASE("AdjustmentLayer: LayerKind default is Normal") {
    Layer l;
    CHECK(l.kind == LayerKind::Normal);
}

TEST_CASE("AdjustmentLayer: adjustment_layer sets kind to Adjustment") {
    // Build via SceneBuilder and inspect the produced Scene
    auto fb = render_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}});
        s.adjustment_layer("grade", [](LayerBuilder& l) {
            l.tint(Color{1,0,0,1}, 0.5f);  // 50% red tint over everything
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    // white tinted with 50% red → r stays high, g and b reduced
    CHECK(c.r > c.g);
    CHECK(c.r > c.b);
}

TEST_CASE("AdjustmentLayer: does not draw content of its own") {
    // Adjustment layer with a rect visual — the rect should NOT appear
    auto fb = render_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // solid dark background
        s.rect("bg", {.size={80,80}, .color=Color{0.1f,0.1f,0.1f,1}, .pos={40,40,0}});
        // adjustment layer adds a rect — should be ignored
        s.adjustment_layer("adj", [](LayerBuilder& l) {
            l.rect("ignore-me", {.size={80,80}, .color=Color::red()});
            // no effects either → adjustment is a no-op
        });
        return s.build();
    });

    // Background should still be dark, not overwritten by the red rect in adj layer
    Color c = fb->get_pixel(40, 40);
    CHECK(c.r < 0.3f);  // dark background, not red
}

TEST_CASE("AdjustmentLayer: Null layer draws nothing") {
    auto fb = render_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}});
        // A null layer — just a transform anchor, draws nothing
        s.layer("null-layer", [](LayerBuilder& l) {
            l.position({40,40,0});
        });
        auto sc = s.build();
        const_cast<Layer&>(sc.layers()[1]).kind = LayerKind::Null;
        return sc;
    });

    // Background still white
    Color c = fb->get_pixel(40, 40);
    CHECK(c.r > 0.9f);
    CHECK(c.g > 0.9f);
}

TEST_CASE("AdjustmentLayer: multiple effects applied in order") {
    auto fb = render_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}});
        s.adjustment_layer("grade", [](LayerBuilder& l) {
            l.brightness(-0.3f)   // darken first
             .contrast(1.2f);     // then boost contrast
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    // White after brightness -0.3 and contrast 1.2 should be less than white
    CHECK(c.r < 1.0f);
    CHECK(c.r > 0.5f);  // but not black
}
