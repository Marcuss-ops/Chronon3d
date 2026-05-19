#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <filesystem>

using namespace chronon3d;

TEST_CASE("Text V2: Stroke and Fill settings rasterization") {
    const std::string font = "assets/fonts/Inter-Regular.ttf";
    if (!std::filesystem::exists(font)) {
        return;
    }

    TextShape t;
    t.text = "Stroke Test";
    t.style.font_path = font;
    t.style.size = 32.0f;
    t.style.paint.fill = Color{1.0f, 0.0f, 0.0f, 1.0f}; // Red fill
    t.style.paint.stroke_enabled = true;
    t.style.paint.stroke_color = Color{0.0f, 0.0f, 1.0f, 1.0f}; // Blue stroke
    t.style.paint.stroke_width = 3.0f;

    auto raster = rasterize_text_to_bl_image(t, 32.0f);
    REQUIRE(raster.has_value());
    CHECK(raster->image.width() > 0);
    CHECK(raster->image.height() > 0);
}

TEST_CASE("Text V2: TextBoxStyle background, border and padding") {
    const std::string font = "assets/fonts/Inter-Regular.ttf";
    if (!std::filesystem::exists(font)) {
        return;
    }

    TextShape t;
    t.text = "Padding and Border Test";
    t.style.font_path = font;
    t.style.size = 32.0f;
    t.box.enabled = true;
    t.box.size = Vec2{300.0f, 150.0f};

    t.style.box_style.enabled = true;
    t.style.box_style.padding = Vec2{15.0f, 10.0f};
    t.style.box_style.radius = 8.0f;
    t.style.box_style.background = Color{0.2f, 0.2f, 0.2f, 1.0f};
    t.style.box_style.border_enabled = true;
    t.style.box_style.border_color = Color{0.8f, 0.8f, 0.8f, 1.0f};
    t.style.box_style.border_width = 2.0f;

    auto raster = rasterize_text_to_bl_image(t, 32.0f);
    REQUIRE(raster.has_value());
    // Bounding image size should match box size + default padding (4)
    CHECK(raster->image.width() == 300 + 4);
    CHECK(raster->image.height() == 150 + 4);
}

TEST_CASE("Text V2: Vertical Alignment Middle & Bottom") {
    const std::string font = "assets/fonts/Inter-Regular.ttf";
    if (!std::filesystem::exists(font)) {
        return;
    }

    TextShape t;
    t.text = "Align Test";
    t.style.font_path = font;
    t.style.size = 32.0f;
    t.box.enabled = true;
    t.box.size = Vec2{200.0f, 200.0f};

    t.style.vertical_align = VerticalAlign::Middle;
    auto raster1 = rasterize_text_to_bl_image(t, 32.0f);
    REQUIRE(raster1.has_value());

    t.style.vertical_align = VerticalAlign::Bottom;
    auto raster2 = rasterize_text_to_bl_image(t, 32.0f);
    REQUIRE(raster2.has_value());
}

TEST_CASE("Text V2: Drop Shadows layout settings") {
    TextStyle style;
    style.font_path = "assets/fonts/Inter-Regular.ttf";
    style.size = 32.0f;

    TextShadow s1;
    s1.enabled = true;
    s1.offset = Vec2{3.0f, 3.0f};
    s1.blur = 4.0f;
    s1.color = Color{0, 0, 0, 0.5f};

    TextShadow s2;
    s2.enabled = true;
    s2.offset = Vec2{-2.0f, -2.0f};
    s2.blur = 2.0f;
    s2.color = Color{1, 0, 0, 0.3f};

    style.shadows.push_back(s1);
    style.shadows.push_back(s2);

    CHECK(style.shadows.size() == 2);
    CHECK(style.shadows[0].enabled);
    CHECK(style.shadows[1].offset.x == -2.0f);
}
