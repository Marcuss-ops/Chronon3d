#include <doctest/doctest.h>

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>

#include <cmath>
#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::animation;

namespace {

void add_motion_content(SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams&) {
    s.layer("card", [ctx](LayerBuilder& l) {
        l.enable_3d()
         .rect("card", {
             .size = {static_cast<f32>(ctx.width) * 0.55f, static_cast<f32>(ctx.height) * 0.35f},
             .color = Color::from_hex("#f2f2f2"),
             .pos = {static_cast<f32>(ctx.width) * 0.5f, static_cast<f32>(ctx.height) * 0.5f, 0.0f},
         })
         .rect("marker", {
             .size = {static_cast<f32>(ctx.width) * 0.08f, static_cast<f32>(ctx.height) * 0.08f},
             .color = Color::from_hex("#d44c4c"),
             .pos = {static_cast<f32>(ctx.width) * 0.36f, static_cast<f32>(ctx.height) * 0.35f, 0.0f},
         })
         .line("slash", {
             .from = {static_cast<f32>(ctx.width) * 0.30f, static_cast<f32>(ctx.height) * 0.25f, 0.0f},
             .to = {static_cast<f32>(ctx.width) * 0.70f, static_cast<f32>(ctx.height) * 0.68f, 0.0f},
             .thickness = 12.0f,
             .color = Color::from_hex("#2b59ff"),
         });
    });
}

void render_motion_clip(MotionAxis axis, const char* name, const char* filename) {
    std::filesystem::create_directories("output/camera_motion");

    SoftwareRenderer renderer;
    CameraMotionParams params;
    params.axis = axis;
    auto comp = chronon3d::presets::camera_motion_clip(name, params, add_motion_content);

    auto scene_start = comp.evaluate(0);
    auto scene_mid = comp.evaluate(30);

    auto fb_start = renderer.render_frame(comp, 0);
    auto fb_mid = renderer.render_frame(comp, 30);

    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_mid != nullptr);
    CHECK(fb_start->width() == fb_mid->width());
    CHECK(fb_start->height() == fb_mid->height());
    CHECK(scene_start.camera_2_5d().enabled);
    CHECK(scene_mid.camera_2_5d().enabled);
    CHECK(scene_start.camera_2_5d().rotation != scene_mid.camera_2_5d().rotation);

    const std::filesystem::path out = std::filesystem::path("output/camera_motion") / filename;
    CHECK(save_png(*fb_mid, out.string()));
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);
}

} // namespace

TEST_CASE("Camera motion clips render a generic scene") {
    SUBCASE("tilt") {
        render_motion_clip(MotionAxis::Tilt, "CameraMotionTiltTest", "tilt.png");
    }

    SUBCASE("pan") {
        render_motion_clip(MotionAxis::Pan, "CameraMotionPanTest", "pan.png");
    }

    SUBCASE("roll") {
        render_motion_clip(MotionAxis::Roll, "CameraMotionRollTest", "roll.png");
    }
}
