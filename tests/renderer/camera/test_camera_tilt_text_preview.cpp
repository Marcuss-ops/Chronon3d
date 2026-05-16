#include <doctest/doctest.h>

#include <Operations/background/dark_grid_background.hpp>

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/presets/studio_presets.hpp>

#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::animation;

namespace {

f32 pixel_delta(const Color& a, const Color& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) + std::abs(a.a - b.a);
}

void build_tilt_text_preview(SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams&) {
    const Vec3 text_pos{0.0f, 0.0f, 0.0f};
    const Vec2 banner_size{
        static_cast<f32>(ctx.width) * 0.34f,
        static_cast<f32>(ctx.height) * 0.16f,
    };
    const Vec3 banner_pos{0.0f, 8.0f, -20.0f};

    // operations::background::dark_grid_background(s, ctx);
    s.layer("background_black", [](LayerBuilder& l) {
        l.rect("bg", {
            .size = {2000, 2000},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });
    });

    s.layer("title", [text_pos](LayerBuilder& l) {
        l.enable_3d();
        l.text("title_text", {
            .content = "TEST",
            .style = {
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 156.0f,
                .color = Color::white(),
                .align = TextAlign::Center
            },
            .pos = text_pos
         }).with_glow(Glow{
             .enabled = true,
             .radius = 26.0f,
             .intensity = 0.9f,
             .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
         });
    });

}

} // namespace

TEST_CASE("Camera tilt text preview renders a centered tilted title PNG") {
    if (!std::filesystem::exists("assets/fonts/Inter-Bold.ttf")) {
        MESSAGE("Skipping tilt text preview because font fixture is missing");
        return;
    }

    CameraMotionParams params;
    params.axis = MotionAxis::Tilt;
    params.start_deg = -10.0f;
    params.end_deg = 0.0f;
    params.duration = 30;
    params.width = 1280;
    params.height = 720;
    params.position = {0.0f, 0.0f, -2200.0f};
    params.zoom = 2200.0f;

    auto comp = chronon3d::presets::camera_motion_clip("CameraTiltTextPreview", params, build_tilt_text_preview);
    SoftwareRenderer renderer;
    renderer.node_cache().clear();
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());

    auto fb = renderer.render_frame(comp, 15);

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1280);
    CHECK(fb->height() == 720);

    float blue_sum = 0.0f;
    for (int y = 0; y < fb->height(); ++y) {
        for (int x = 0; x < fb->width(); ++x) {
            blue_sum += fb->get_pixel(x, y).b;
        }
    }
    CHECK(blue_sum > 0.05f);

    const std::filesystem::path out = "output/camera_motion/tilt_text_preview_test.png";
    std::filesystem::create_directories(out.parent_path());
    CHECK(save_png(*fb, out.string()));
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);
}
