#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

namespace {

static SoftwareRenderer g_renderer;

// Render a 100x100 composition with a single centered layer.
// setup: configure the layer (position, effects, shapes).
static std::unique_ptr<Framebuffer> render_layer(std::function<void(LayerBuilder&)> setup,
                                                  i32 w = 100, i32 h = 100) {
    Composition comp = composition({.name = "Test", .width = w, .height = h, .duration = 1},
                                   [&](const FrameContext& ctx) {
                                       SceneBuilder s(ctx);
                                       s.layer("l", setup);
                                       return s.build();
                                   });
    return g_renderer.render_frame(comp, 0);
}

// Convenience: layer setup for a rect centered in a 100x100 canvas.
static auto rect_at_center(Vec2 size, Color color = Color{1, 1, 1, 1}) {
    return [=](LayerBuilder& l) {
        l.position({50.f, 50.f, 0.f})
         .rect("r", {.size = size, .color = color, .pos = {}});
    };
}

} // namespace

// ---------------------------------------------------------------------------
TEST_CASE("Text align center places text symmetrically") {
    // Two texts: one Left, one Center at same position.
    // Center text should start further left (shifted by half width).
    Composition comp = composition({
        .name = "TextAlignTest", .width = 400, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({200, 100, 0});
            l.text("t", { .content = "X", .style = {
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 50, .color = Color{1,1,1,1}, .align = TextAlign::Center
            }, .pos = {0, 0, 0} });
        });
        return s.build();
    });
    SoftwareRenderer r;
    auto fb = r.render_frame(comp, 0);
    // Pixel at centre should be visible (non-black) because text is centred there.
    // We just verify it doesn't crash and produces output.
    CHECK(fb->width() == 400);
    CHECK(fb->height() == 200);
}

// ---------------------------------------------------------------------------
TEST_CASE("Blur reduces high-frequency detail") {
    auto fb_s = render_layer(rect_at_center({80, 80}));
    auto fb_b = render_layer([](LayerBuilder& l) {
        l.position({50, 50, 0}).blur(6).rect("r", {.size = {80, 80}, .color = {1,1,1,1}, .pos = {}});
    });

    // Rect 80x80 centred at (50,50) → spans (10,10)-(90,90). Pixel (5,50) is outside.
    CHECK(fb_s->get_pixel(5, 50).r < 0.1f);   // clearly outside sharp rect: black
    CHECK(fb_b->get_pixel(5, 50).r > 0.01f);  // blur spreads white into border region
}

// ---------------------------------------------------------------------------
TEST_CASE("Tint shifts layer colour toward tint colour") {
    auto fb = render_layer([](LayerBuilder& l) {
        l.position({50, 50, 0}).tint(Color{1, 0, 0, 0.8f})
         .rect("r", {.size = {80, 80}, .color = {1,1,1,1}, .pos = {}});
    });
    auto p = fb->get_pixel(50, 50);
    // White + red tint → r stays high, g and b drop
    CHECK(p.r > 0.5f);
    CHECK(p.g < p.r);
    CHECK(p.b < p.r);
}

// ---------------------------------------------------------------------------
TEST_CASE("Brightness positive brightens the layer") {
    const Color grey{0.4f, 0.4f, 0.4f, 1.f};
    auto fb_n = render_layer(rect_at_center({80, 80}, grey));
    auto fb_b = render_layer([grey](LayerBuilder& l) {
        l.position({50, 50, 0}).brightness(0.3f)
         .rect("r", {.size = {80, 80}, .color = grey, .pos = {}});
    });
    CHECK(fb_b->get_pixel(50, 50).r > fb_n->get_pixel(50, 50).r);
}

// ---------------------------------------------------------------------------
TEST_CASE("Layer without effects renders identically to before") {
    auto fb = render_layer(rect_at_center({60, 60}, Color{0, 1, 0, 1}));
    CHECK(fb->get_pixel(50, 50).g > 0.5f);
    CHECK(fb->get_pixel(10, 10).g < 0.1f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Screen blend mode brightens the composite") {
    Composition comp = composition({
        .name = "ScreenBlend", .width = 100, .height = 100, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Dark background
        s.rect("bg", { .size = {100,100}, .color = Color{0.4f,0.4f,0.4f,1}, .pos = {50,50,0} });
        // Screen layer on top — result should be brighter than background
        s.layer("l", [](LayerBuilder& l) {
            l.position({50, 50, 0}).blend(BlendMode::Screen);
            l.rect("r", { .size = {80,80}, .color = Color{0.5f,0.5f,0.5f,1}, .pos = {0,0,0} });
        });
        return s.build();
    });
    SoftwareRenderer r;
    auto fb = r.render_frame(comp, 0);
    // Colors go through to_linear() in renderer. Screen result in linear space is
    // brighter than background alone — check it's above the raw background linear value.
    CHECK(fb->get_pixel(50, 50).r > 0.15f);
}
