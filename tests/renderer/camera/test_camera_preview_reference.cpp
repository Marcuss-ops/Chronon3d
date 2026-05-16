#include <doctest/doctest.h>

#include <chronon3d/scene/utils/dark_grid_background.hpp>

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>

#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::animation;

namespace {

void build_preview_reference(SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
    const f32 inset_x = static_cast<f32>(ctx.width) * 0.12f;
    const f32 inset_y = static_cast<f32>(ctx.height) * 0.12f;
    const Vec2 image_size{
        static_cast<f32>(ctx.width) - inset_x * 2.0f,
        static_cast<f32>(ctx.height) - inset_y * 2.0f,
    };
    const Vec3 frame_pos{
        static_cast<f32>(ctx.width) * 0.5f,
        static_cast<f32>(ctx.height) * 0.5f,
        0.0f,
    };

    scene::utils::dark_grid_background(s, ctx);

    s.camera().set({
        .enabled = true,
        .position = {0.0f, 0.0f, -2200.0f},
        .zoom = 2200.0f,
    });

    s.layer("reference-frame", [image_size, frame_pos](LayerBuilder& l) {
        l.enable_3d()
         .rounded_rect("reference-frame-bg", {
             .size = {image_size.x + 56.0f, image_size.y + 56.0f},
             .radius = 18.0f,
             .color = Color::from_hex("#f5f7fb"),
             .pos = frame_pos,
         })
         .drop_shadow({0.0f, 10.0f}, Color{0.0f, 0.0f, 0.0f, 0.34f}, 20.0f);
    });

    s.layer("reference-image", [reference_image = p.reference_image, image_size, frame_pos](LayerBuilder& l) {
        l.enable_3d()
         .image("grid_reference", {
             .path = reference_image,
             .size = image_size,
             .pos = frame_pos,
             .opacity = 1.0f,
         });
    });
}

f32 pixel_delta(const Color& a, const Color& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) + std::abs(a.a - b.a);
}

} // namespace

TEST_CASE("Camera preview reference stays centered and visible") {
    CameraMotionParams params;
    params.axis = MotionAxis::Roll;
    params.start_deg = -4.0f;
    params.end_deg = 0.0f;
    params.duration = 30;
    params.width = 1280;
    params.height = 720;
    params.reference_image = "assets/images/camera_reference.jpg";

    auto comp = chronon3d::presets::camera_motion_clip("CameraPreviewReference", params, build_preview_reference);
    SoftwareRenderer renderer;
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    auto fb = renderer.render_frame(comp, 0);

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1280);
    CHECK(fb->height() == 720);

    const Color center = fb->get_pixel(640, 360);
    CHECK(center.a > 0.9f);
    CHECK(pixel_delta(center, fb->get_pixel(10, 10)) > 0.05f);

    const std::filesystem::path out = "output/camera_motion/camera_preview_reference_test.png";
    CHECK(save_png(*fb, out.string()));
    CHECK(std::filesystem::exists(out));
}
