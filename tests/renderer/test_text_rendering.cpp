#include <doctest/doctest.h>
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <filesystem>

using namespace chronon3d;

TEST_CASE("SceneBuilder creates text node") {
    SceneBuilder s;

    TextStyle style;
    style.font_path = "assets/fonts/Inter-Regular.ttf";
    style.size = 32.0f;
    style.color = Color{1, 1, 1, 1};

    s.text("hello", { .content = "Hello", .style = style, .pos = {10, 20, 0} });

    auto scene = s.build();

    REQUIRE(scene.nodes().size() == 1);
    REQUIRE(scene.nodes()[0].shape.type == ShapeType::Text);
    REQUIRE(scene.nodes()[0].shape.text.text == "Hello");
    CHECK(scene.nodes()[0].world_transform.position.x == doctest::Approx(10.0f));
    CHECK(scene.nodes()[0].world_transform.position.y == doctest::Approx(20.0f));
}

TEST_CASE("Text renderer fails safely on missing font") {
    Framebuffer fb(320, 180);
    fb.clear(Color{0, 0, 0, 1});

    TextShape text;
    text.text = "Hello";
    text.style.font_path = "assets/fonts/does_not_exist.ttf";
    text.style.size = 32.0f;
    text.style.color = Color{1, 1, 1, 1};

    Transform tr;
    tr.position = {20, 40, 0};

    TextRenderer renderer;

    REQUIRE_FALSE(renderer.draw_text(text, tr, fb));
}

TEST_CASE("Text renderer produces non-empty pixels") {
    const std::string font = "assets/fonts/Inter-Regular.ttf";

    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping text test because font fixture is missing");
        return;
    }

    Framebuffer fb(500, 220);
    fb.clear(Color{0, 0, 0, 1});

    TextShape text;
    text.text = "Hello";
    text.style.font_path = font;
    text.style.size = 64.0f;
    text.style.color = Color{1, 1, 1, 1};

    Transform tr;
    tr.position = {30, 60, 0};

    TextRenderer renderer;

    REQUIRE(renderer.draw_text(text, tr, fb));

    bool found = false;

    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);

            if (p.r > 0.01f || p.g > 0.01f || p.b > 0.01f) {
                found = true;
                break;
            }
        }

        if (found) break;
    }

    REQUIRE(found);
}

TEST_CASE("Text alpha affects rendered pixels") {
    const std::string font = "assets/fonts/Inter-Regular.ttf";

    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping text alpha test because font fixture is missing");
        return;
    }

    Framebuffer opaque_fb(500, 220);
    Framebuffer half_fb(500, 220);

    opaque_fb.clear(Color{0, 0, 0, 1});
    half_fb.clear(Color{0, 0, 0, 1});

    TextShape text;
    text.text = "Alpha";
    text.style.font_path = font;
    text.style.size = 64.0f;
    text.style.color = Color{1, 1, 1, 1.0f};

    Transform tr;
    tr.position = {30, 60, 0};

    Transform tr_half = tr;
    tr_half.opacity = 0.5f;

    TextRenderer renderer;
    REQUIRE(renderer.draw_text(text, tr, opaque_fb));
    REQUIRE(renderer.draw_text(text, tr_half, half_fb));

    float opaque_sum = 0.0f;
    float half_sum = 0.0f;

    for (int y = 0; y < opaque_fb.height(); ++y) {
        for (int x = 0; x < opaque_fb.width(); ++x) {
            auto a = opaque_fb.get_pixel(x, y);
            auto b = half_fb.get_pixel(x, y);

            opaque_sum += a.r + a.g + a.b;
            half_sum += b.r + b.g + b.b;
        }
    }

    CHECK(opaque_sum > half_sum);
    CHECK(half_sum > 0.0f);
}

TEST_CASE("Text respects draw order") {
    const std::string font = "assets/fonts/Inter-Regular.ttf";

    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping text draw order test because font fixture is missing");
        return;
    }

    SceneBuilder s;

    s.rect("bg", { .size = {300, 150}, .color = Color{0, 0, 0, 1}, .pos = {150, 75, 0} });

    s.text("text", {
        .content = "HI",
        .style = {
            .font_path = font,
            .size = 72.0f,
            .color = Color{1, 1, 1, 1}
        },
        .pos = {60, 35, 0}
    });

    s.rect("overlay", { .size = {300, 150}, .color = Color{1, 0, 0, 0.35f}, .pos = {150, 75, 0} });

    auto scene = s.build();

    SoftwareRenderer renderer;
    Camera cam;
    auto fb = renderer.render_scene(scene, cam, 300, 150);

    REQUIRE(fb != nullptr);
}
