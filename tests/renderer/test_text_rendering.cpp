#include <doctest/doctest.h>
#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <filesystem>

using namespace chronon3d;

TEST_CASE("Text rendering produces non-empty pixels") {
    const std::string font_path = "assets/fonts/Inter-Regular.ttf";
    
    // Skip test if font is missing
    if (!std::filesystem::exists(font_path)) {
        return;
    }

    Framebuffer fb(400, 200);
    fb.clear(Color{0, 0, 0, 1});

    TextShape text_shape;
    text_shape.text = "CHRONON";
    text_shape.position = {20, 100, 0};
    text_shape.style.font_path = font_path;
    text_shape.style.size = 48;
    text_shape.style.color = Color{1, 1, 1, 1};

    TextRenderer renderer;
    bool success = renderer.draw_text(text_shape, fb);
    CHECK(success);

    // Verify that some pixels are no longer black
    bool found_non_black = false;
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color p = fb.get_pixel(x, y);
            if (p.r > 0.01f || p.g > 0.01f || p.b > 0.01f) {
                found_non_black = true;
                break;
            }
        }
        if (found_non_black) break;
    }

    CHECK(found_non_black);
}

TEST_CASE("Text rendering fails gracefully on missing font") {
    Framebuffer fb(400, 200);
    fb.clear(Color{0, 0, 0, 1});

    TextShape text_shape;
    text_shape.text = "Hello";
    text_shape.position = {20, 100, 0};
    text_shape.style.font_path = "non_existent_font.ttf";
    text_shape.style.size = 32;

    TextRenderer renderer;
    bool success = renderer.draw_text(text_shape, fb);
    CHECK_FALSE(success);
}

TEST_CASE("SceneBuilder creates text nodes") {
    SceneBuilder s;
    TextStyle style;
    style.font_path = "test.ttf";
    style.size = 24;
    
    s.text("test_id", "Content", {10, 20, 30}, style);
    Scene scene = s.build();

    CHECK(scene.nodes().size() == 1);
    const RenderNode& node = scene.nodes()[0];
    CHECK(node.name == "test_id");
    CHECK(node.shape.type == ShapeType::Text);
    CHECK(node.shape.text.text == "Content");
    CHECK(node.shape.text.style.size == 24);
}
