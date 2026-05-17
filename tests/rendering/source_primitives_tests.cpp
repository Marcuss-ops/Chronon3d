#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/chronon3d.hpp>
#include <filesystem>

using namespace chronon3d;

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());
    return renderer;
}

std::string make_white_image() {
    const std::filesystem::path dir = "output/debug/primitives";
    std::filesystem::create_directories(dir);
    const auto path = dir / "white.png";
    if (std::filesystem::exists(path)) {
        return path.string();
    }
    Framebuffer fb(32, 32);
    fb.clear(Color::white());
    save_png(fb, path.string());
    return path.string();
}

class MockVideoDecoder : public video::VideoFrameDecoder {
public:
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string&,
        Frame frame,
        i32 width,
        i32 height,
        f32
    ) override {
        auto fb = std::make_shared<Framebuffer>(width, height);
        // Draw blue color if frame is 5, red if not
        if (frame == 5) {
            fb->clear(Color::blue());
        } else {
            fb->clear(Color::red());
        }
        return fb;
    }
};

} // namespace

TEST_CASE("Test 13.1 — Rect primitive rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("r", {.size = {40, 40}, .color = Color::red(), .pos = {50, 50, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.9f);
    CHECK(fb->get_pixel(10, 10).a == 0.0f);
}

TEST_CASE("Test 13.2 — Rounded rect primitive rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rounded_rect("rr", {.size = {40, 40}, .radius = 8.0f, .color = Color::red(), .pos = {50, 50, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.9f);
    // Corners rounded away
    CHECK(fb->get_pixel(31, 31).a == 0.0f);
}

TEST_CASE("Test 13.3 — Circle primitive rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.circle("c", {.radius = 20.0f, .color = Color::red(), .pos = {50, 50, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.9f);
    // Beyond radius 20
    CHECK(fb->get_pixel(80, 50).a == 0.0f);
}

TEST_CASE("Test 13.4 — Line primitive rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.line("l", {.from = {10, 50, 0}, .to = {90, 50, 0}, .thickness = 4.0f, .color = Color::red()});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.9f);
    CHECK(fb->get_pixel(50, 40).a == 0.0f);
}

TEST_CASE("Test 13.5 — Text primitive rendering") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) return;

    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [&](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        TextStyle style{.font_path = font, .size = 30.0f, .color = Color::red()};
        s.text("t", {.content = "A", .style = style, .pos = {50, 50, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.0f);
}

TEST_CASE("Test 13.6 — Image primitive rendering") {
    const std::string white_img = make_white_image();
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [&](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.image("img", {.path = white_img, .size = {40, 40}, .pos = {50, 50, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.9f);
}

TEST_CASE("Test 13.7 — FakeBox3D primitive rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set({.enabled = true, .position = {0,0,-500}, .zoom = 500.0f});
        s.layer("b", [](LayerBuilder& l) {
            l.enable_3d();
            l.fake_box3d("box", {.pos = {0,0,0}, .size = {40, 40}, .depth = 10.0f, .color = Color::red()});
        });
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.5f);
}

TEST_CASE("Test 13.8 — FakeExtrudedText primitive rendering") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) return;

    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [&](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set({.enabled = true, .position = {0,0,-500}, .zoom = 500.0f});
        s.layer("t", [&](LayerBuilder& l) {
            l.enable_3d();
            l.fake_extruded_text("text", {
                .text = "H",
                .font_path = font,
                .pos = {0,0,0},
                .font_size = 40.0f,
                .depth = 5,
                .front_color = Color::red()
            });
        });
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).r > 0.0f);
}

TEST_CASE("Test 13.9 — GridPlane primitive rendering") {
    auto renderer = make_renderer();
    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set({.enabled = true, .position = {0,0,-500}, .zoom = 500.0f});
        s.layer("gp", [](LayerBuilder& l) {
            l.enable_3d();
            l.grid_plane("grid", {
                .pos = {0,0,0},
                .extent = 100.0f,
                .spacing = 10.0f,
                .color = Color::red()
            });
        });
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
}

TEST_CASE("Test 13.10 — Video layer runtime frame sampling") {
    auto renderer = make_renderer();
    auto mock_decoder = std::make_shared<MockVideoDecoder>();
    renderer.set_video_decoder(mock_decoder);

    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.video_layer("vid", video::VideoSource{.path = "clip.mp4"}, [](LayerBuilder&) {});
        return s.build();
    });

    // Render frame 5: should query frame 5 from the decoder and yield blue
    auto fb = renderer.render_frame(comp, 5);
    REQUIRE(fb != nullptr);
    CHECK(fb->get_pixel(50, 50).b > 0.9f);
    CHECK(fb->get_pixel(50, 50).r < 0.2f);
}

TEST_CASE("Test 13.11 — Precomp layer subcomp sampling") {
    auto renderer = make_renderer();
    CompositionRegistry registry;

    // Register nested comp "sub" which renders a solid green rect
    registry.add("sub", []() {
        return Composition({.name = "sub", .width = 100, .height = 100}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("green_box", {.size = {60, 60}, .color = Color::green(), .pos = {50, 50, 0}});
            return s.build();
        });
    });

    Composition comp({.width = 100, .height = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "sub", [](LayerBuilder&) {});
        return s.build();
    });

    // Run composition with registry bound to settings/executor
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_composition_registry(&registry);

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    // Center should be green (0.0, 1.0, 0.0) from the precomp layer
    CHECK(fb->get_pixel(50, 50).g > 0.9f);
    CHECK(fb->get_pixel(50, 50).r < 0.2f);
}
