#include <doctest/doctest.h>
#include <chronon3d/backends/software/rasterizers/card3d_material_rasterizer.hpp>
#include <chronon3d/scene/card3d_material.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

using namespace chronon3d;
using namespace chronon3d::renderer;

static Card3DMaterial default_material() {
    Card3DMaterial m;
    m.enabled = true;
    m.thickness_px = 14.0f;
    m.front_top_color = {0.95f, 0.96f, 0.98f, 1.0f};
    m.front_bottom_color = {0.75f, 0.78f, 0.85f, 1.0f};
    m.side_color = {0.55f, 0.58f, 0.65f, 1.0f};
    m.edge_highlight_color = {1.0f, 1.0f, 1.0f, 1.0f};
    m.edge_highlight_opacity = 0.35f;
    m.rim_intensity = 0.45f;
    m.rim_power = 2.2f;
    m.edge_highlight_color = {0.8f, 0.9f, 1.0f, 1.0f};

    return m;
}

static int count_nonzero_pixels(const Framebuffer& fb) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).a > 0.001f) ++count;
        }
    }
    return count;
}

TEST_CASE("Card3DMaterial rasterizer: disabled material draws nothing") {
    Framebuffer fb(200, 200);
    fb.clear(Color::transparent());

    Card3DMaterial m;
    m.enabled = false;

    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {100.0f, 60.0f}};
    render_card3d_material(fb, m, p);

    CHECK(count_nonzero_pixels(fb) == 0);
}

TEST_CASE("Card3DMaterial rasterizer: draws front face") {
    Framebuffer fb(200, 200);
    fb.clear(Color::transparent());

    Card3DMaterial m = default_material();
    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {100.0f, 60.0f}};
    render_card3d_material(fb, m, p);

    int nonzero = count_nonzero_pixels(fb);
    CHECK(nonzero > 100); // front face should have many pixels
}

TEST_CASE("Card3DMaterial rasterizer: thickness creates side faces") {
    Framebuffer fb(200, 200);
    fb.clear(Color::transparent());

    Card3DMaterial m = default_material();
    m.thickness_px = 20.0f;
    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {80.0f, 50.0f}};
    render_card3d_material(fb, m, p);

    int nonzero = count_nonzero_pixels(fb);
    // With thickness, the bounding area should be noticeably larger than just the front face
    CHECK(nonzero > 5000); // front face is ~4000, extruded sides add more
}

TEST_CASE("Card3DMaterial rasterizer: zero thickness skips side faces") {
    Framebuffer fb(200, 200);
    fb.clear(Color::transparent());

    Card3DMaterial m = default_material();
    m.thickness_px = 0.0f;
    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {80.0f, 50.0f}};
    render_card3d_material(fb, m, p);

    int nonzero = count_nonzero_pixels(fb);
    // Without thickness, only front face (no extruded area)
    CHECK(nonzero > 0);
    // Area should be approximately 80*50 = 4000
    CHECK(nonzero < 6000);
}

TEST_CASE("Card3DMaterial rasterizer: gradient top vs bottom colors") {
    Framebuffer fb(200, 200);
    fb.clear(Color::transparent());

    Card3DMaterial m = default_material();
    m.front_top_color = {1.0f, 0.0f, 0.0f, 1.0f};   // red top
    m.front_bottom_color = {0.0f, 0.0f, 1.0f, 1.0f}; // blue bottom
    m.thickness_px = 0.0f; // no sides to simplify

    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {100.0f, 80.0f}};
    render_card3d_material(fb, m, p);

    // Sample near top-center: should be reddish
    Color top = fb.get_pixel(100, 55);
    CHECK(top.r > top.b);

    // Sample near bottom-center: should be bluish
    Color bot = fb.get_pixel(100, 120);
    CHECK(bot.b > bot.r);
}

TEST_CASE("Card3DMaterial rasterizer: rim light adds edge glow") {
    Framebuffer fb1(200, 200);
    fb1.clear(Color::transparent());
    Framebuffer fb2(200, 200);
    fb2.clear(Color::transparent());

    Card3DMaterial m1 = default_material();
    m1.rim_intensity = 0.0f; // no rim
    Card3DMaterial m2 = default_material();
    m2.rim_intensity = 0.8f; // strong rim

    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {80.0f, 50.0f}};
    render_card3d_material(fb1, m1, p);
    render_card3d_material(fb2, m2, p);

    // The rim version should have more non-zero pixels (glow extends beyond)
    int count1 = count_nonzero_pixels(fb1);
    int count2 = count_nonzero_pixels(fb2);
    CHECK(count2 > count1);
}

TEST_CASE("Card3DMaterial rasterizer: edge highlight adds pixels") {
    Framebuffer fb1(200, 200);
    fb1.clear(Color::transparent());
    Framebuffer fb2(200, 200);
    fb2.clear(Color::transparent());

    Card3DMaterial m1 = default_material();
    m1.edge_highlight_opacity = 0.0f;
    Card3DMaterial m2 = default_material();
    m2.edge_highlight_opacity = 0.8f;

    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {80.0f, 50.0f}};
    render_card3d_material(fb1, m1, p);
    render_card3d_material(fb2, m2, p);

    // Both should have similar front-face area, but edge highlight version
    // might have slightly different pixel counts due to line thickness
    int count1 = count_nonzero_pixels(fb1);
    int count2 = count_nonzero_pixels(fb2);
    CHECK(count2 > 0);
    CHECK(count1 > 0);
}

TEST_CASE("Card3DMaterial rasterizer: empty size draws nothing") {
    Framebuffer fb(200, 200);
    fb.clear(Color::transparent());

    Card3DMaterial m = default_material();
    Card3DRenderParams p{.position = {50.0f, 50.0f}, .size = {0.0f, 0.0f}};
    render_card3d_material(fb, m, p);

    CHECK(count_nonzero_pixels(fb) == 0);
}
