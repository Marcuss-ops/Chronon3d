#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software_renderer.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
TEST_CASE("Mask rect clips layer content") {
    Composition comp = composition({
        .name = "MaskRectClipTest", .width = 200, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("masked", [](LayerBuilder& l) {
            l.position({100, 100, 0})
             .mask_rect({ .size = {60, 60}, .pos = {0, 0, 0} });
            l.rect("big", { .size = {160, 160}, .color = Color{1, 0, 0, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Centre of mask: red
    CHECK(fb->get_pixel(100, 100).r > 0.5f);
    // Outside mask but inside original big rect: clipped → black
    CHECK(fb->get_pixel(20, 100).r  < 0.1f);
    CHECK(fb->get_pixel(180, 100).r < 0.1f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Mask rounded rect clips content with rounded corners") {
    Composition comp = composition({
        .name = "MaskRoundedRectTest", .width = 200, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("masked", [](LayerBuilder& l) {
            l.position({100, 100, 0})
             .mask_rounded_rect({ .size = {100, 100}, .radius = 30, .pos = {0, 0, 0} });
            l.rect("fill", { .size = {160, 160}, .color = Color{0, 1, 0, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Centre inside: green
    CHECK(fb->get_pixel(100, 100).g > 0.5f);
    // Corner pixel (near top-left corner outside radius): clipped
    CHECK(fb->get_pixel(55, 55).g < 0.1f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Mask circle clips content to a circle") {
    Composition comp = composition({
        .name = "MaskCircleTest", .width = 200, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("masked", [](LayerBuilder& l) {
            l.position({100, 100, 0})
             .mask_circle({ .radius = 40, .pos = {0, 0, 0} });
            l.rect("fill", { .size = {160, 160}, .color = Color{0, 0, 1, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Centre: inside circle → blue
    CHECK(fb->get_pixel(100, 100).b > 0.5f);
    // Near corner of enclosing square but outside circle: clipped
    CHECK(fb->get_pixel(135, 135).b < 0.1f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Layer without mask renders normally") {
    Composition comp = composition({
        .name = "NoMaskTest", .width = 200, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("normal", [](LayerBuilder& l) {
            l.position({100, 100, 0});
            l.rect("big", { .size = {160, 160}, .color = Color{1, 0, 0, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Without mask, pixel far from centre is still red
    CHECK(fb->get_pixel(30, 100).r > 0.5f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Animated mask size changes visible area") {
    Composition comp = composition({
        .name = "AnimMaskTest", .width = 200, .height = 200, .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const auto w = interpolate(ctx.frame, 0, 30, 20.0f, 140.0f);
        s.layer("masked", [&](LayerBuilder& l) {
            l.position({100, 100, 0})
             .mask_rect({ .size = {w, 100}, .pos = {0, 0, 0} });
            l.rect("content", { .size = {140, 100}, .color = Color{1, 0, 0, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    // Frame 0: mask width ~20 → right edge at ~110 is outside
    auto fb0  = renderer.render_frame(comp, 0);
    // Frame 30: mask width ~140 → right edge at ~170 is inside
    auto fb30 = renderer.render_frame(comp, 30);

    CHECK(fb0->get_pixel(155, 100).r  < 0.1f);
    CHECK(fb30->get_pixel(155, 100).r > 0.5f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Mask follows layer position") {
    Composition comp = composition({
        .name = "MaskTransformTest", .width = 300, .height = 300, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("masked", [](LayerBuilder& l) {
            l.position({200, 150, 0})
             .mask_rect({ .size = {60, 60}, .pos = {0, 0, 0} });
            l.rect("big", { .size = {160, 160}, .color = Color{1, 0, 0, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Layer is at (200,150): centre of mask should be red
    CHECK(fb->get_pixel(200, 150).r > 0.5f);
    // Original layer origin (100,150) is outside the mask window
    CHECK(fb->get_pixel(100, 150).r < 0.1f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Inverted mask clips outside-in") {
    Composition comp = composition({
        .name = "InvertedMaskTest", .width = 200, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("masked", [](LayerBuilder& l) {
            l.position({100, 100, 0})
             .mask_rect({ .size = {60, 60}, .pos = {0, 0, 0}, .inverted = true });
            l.rect("fill", { .size = {160, 160}, .color = Color{1, 0, 0, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Centre is inside the inverted mask → clipped (black)
    CHECK(fb->get_pixel(100, 100).r < 0.1f);
    // Outside the mask rect but inside big rect → visible
    CHECK(fb->get_pixel(30, 100).r  > 0.5f);
}
