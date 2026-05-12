#include <doctest/doctest.h>
#include <chronon3d/renderer/framebuffer.hpp>
#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

using namespace chronon3d;

TEST_CASE("Framebuffer and Color") {
    Framebuffer fb(100, 100);

    SUBCASE("Clear") {
        fb.clear(Color::red());
        Color p = fb.get_pixel(50, 50);
        CHECK(p.r == 1.0f);
        CHECK(p.g == 0.0f);
        CHECK(p.b == 0.0f);
    }

    SUBCASE("Set Pixel") {
        fb.set_pixel(10, 10, Color::blue());
        Color p = fb.get_pixel(10, 10);
        CHECK(p.b == 1.0f);
    }
}

TEST_CASE("Blending") {
    Color src(1.0f, 0.0f, 0.0f, 0.5f); // Semi-transparent Red
    Color dst(0.0f, 0.0f, 1.0f, 1.0f); // Opaque Blue

    SUBCASE("Normal Blend") {
        Color result = compositor::blend(src, dst, BlendMode::Normal);
        // Result should be a mix of red and blue
        CHECK(result.r == 0.5f);
        CHECK(result.b == 0.5f);
        CHECK(result.a == 1.0f);
    }

    SUBCASE("Add Blend") {
        Color result = compositor::blend(src, dst, BlendMode::Add);
        CHECK(result.r == 1.0f);
        CHECK(result.b == 1.0f);
    }
}

TEST_CASE("Software Rendering Integration") {
    Composition::Builder builder;
    builder.name("TestRender").size(200, 200).fps({30, 1});
    
    auto& layer = builder.add_layer("Rect", LayerType::Shape);
    layer.range = {0, 10};
    layer.transform.position.set(Vec3(100.0f, 100.0f, 0.0f));
    layer.opacity.set(1.0f);

    auto comp = builder.build();

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(*comp, 0);

    SUBCASE("Verify center pixel") {
        Color center = fb->get_pixel(100, 100);
        CHECK(center.r == 1.0f);
        CHECK(center.g == 1.0f);
        CHECK(center.b == 1.0f);
    }

    SUBCASE("Verify background pixel") {
        Color bg = fb->get_pixel(0, 0);
        CHECK(bg.r == 0.0f);
        CHECK(bg.g == 0.0f);
        CHECK(bg.b == 0.0f);
    }
}
