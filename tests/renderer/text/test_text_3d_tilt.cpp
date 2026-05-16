#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <algorithm>
#include <filesystem>
#include <utility>

using namespace chronon3d;

namespace {

std::shared_ptr<Framebuffer> render_tilt_text_frame(float tilt_x_deg) {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) return nullptr;

    SceneBuilder s;
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("black", {.size = {2000, 2000}, .color = Color::black()});
    });
    s.layer("tilted_text", [&](LayerBuilder& l) {
        l.enable_3d();
        l.text("text", {
            .content = "PERSPECTIVE",
            .style = {
                .font_path = font,
                .size = 100.0f,
                .color = Color::white(),
                .align = TextAlign::Center
            }
        }).at({640, 360, 0}).with_glow({
            .enabled = true,
            .radius = 20.0f,
            .intensity = 1.0f,
            .color = Color::white()
        });
        
        l.rotate({tilt_x_deg, 0.0f, 0.0f});
    });

    SoftwareRenderer renderer;
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());
    
    // Use a camera that is closer to see more perspective
    Camera2_5D cam;
    cam.position = {640, 360, -500};
    cam.zoom = 500;
    
    auto scene = s.build();
    scene.set_camera_2_5d(cam);

    auto fb = renderer.render_scene(scene, std::nullopt, 1280, 720);
    REQUIRE(fb != nullptr);
    return fb;
}

std::pair<int, int> bright_bounds_y(const Framebuffer& fb) {
    int y_min = fb.height();
    int y_max = -1;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const auto p = fb.get_pixel(x, y);
            if ((p.r + p.g + p.b) > 0.1f) {
                y_min = std::min(y_min, y);
                y_max = std::max(y_max, y);
            }
        }
    }
    return {y_min, y_max};
}

} // namespace

TEST_CASE("Text respects 2.5D layer tilt with perspective") {
    const auto flat_fb = render_tilt_text_frame(0.0f);
    const auto tilted_fb = render_tilt_text_frame(60.0f);

    REQUIRE(flat_fb != nullptr);
    REQUIRE(tilted_fb != nullptr);

    const auto [flat_y0, flat_y1] = bright_bounds_y(*flat_fb);
    const auto [tilt_y0, tilt_y1] = bright_bounds_y(*tilted_fb);

    REQUIRE(flat_y1 >= flat_y0);
    REQUIRE(tilt_y1 >= tilt_y0);

    const int flat_height = flat_y1 - flat_y0 + 1;
    const int tilt_height = tilt_y1 - tilt_y0 + 1;

    // Tilt should compress the glyph block vertically.
    CHECK(tilt_height < flat_height);

    const std::string out = "output/debug/tilted_text_test.png";
    std::filesystem::create_directories("output/debug");
    save_png(*tilted_fb, out);
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);
}
