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
    const Vec3 text_pos{
        static_cast<f32>(ctx.width) * 0.5f,
        static_cast<f32>(ctx.height) * 0.5f,
        0.0f,
    };
    const Vec2 banner_size{
        static_cast<f32>(ctx.width) * 0.34f,
        static_cast<f32>(ctx.height) * 0.16f,
    };
    const Vec3 banner_pos{
        static_cast<f32>(ctx.width) * 0.5f,
        static_cast<f32>(ctx.height) * 0.5f + 8.0f,
        -20.0f,
    };

    operations::background::dark_grid_background(s, ctx);

    s.layer("banner", [banner_size, banner_pos](LayerBuilder& l) {
        l.enable_3d()
         .rounded_rect("banner_bg", {
             .size = banner_size,
             .radius = 6.0f,
             .color = Color::from_hex("#ea3b36"),
             .pos = banner_pos,
         })
         .drop_shadow({0.0f, 12.0f}, Color{0.0f, 0.0f, 0.0f, 0.28f}, 18.0f);
    });

    s.layer("title", [text_pos](LayerBuilder& l) {
        l.enable_3d()
         .drop_shadow({0.0f, 8.0f}, Color{0.0f, 0.0f, 0.0f, 0.26f}, 8.0f)
         .with_glow(Glow{
             .enabled = true,
             .radius = 14.0f,
             .intensity = 0.16f,
             .color = Color{1.0f, 1.0f, 1.0f, 0.16f},
         });
        l.fake_extruded_text("title_text", {
            .text = "TEST",
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .pos = text_pos,
            .font_size = 156.0f,
            .depth = 8,
            .extrude_dir = {0.25f, 0.75f},
            .extrude_z_step = 0.85f,
            .front_color = Color::white(),
            .side_color = Color::from_hex("#c9c9d1"),
            .side_fade = 0.14f,
            .align = TextAlign::Center,
            .highlight_opacity = 0.04f,
            .bevel_size = 0.55f,
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
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());

    auto fb = renderer.render_frame(comp, 15);

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1280);
    CHECK(fb->height() == 720);

    const Color center = fb->get_pixel(640, 360);
    const Color corner = fb->get_pixel(10, 10);
    CHECK(center.a > 0.9f);
    CHECK(pixel_delta(center, corner) > 0.05f);

    const std::filesystem::path out = "output/camera_motion/tilt_text_preview_test.png";
    std::filesystem::create_directories(out.parent_path());
    CHECK(save_png(*fb, out.string()));
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);
}
