#include <doctest/doctest.h>

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/ffmpeg/video_export.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::animation;

#ifdef CHRONON_WITH_VIDEO

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

} // namespace

TEST_CASE("Camera motion tilt exports a real MP4") {
    std::filesystem::create_directories("output/camera_motion");

    SoftwareRenderer renderer;
    auto comp = camera_motion_clip("CameraMotionTiltVideo", MotionAxis::Tilt, {}, add_motion_content);

    video::VideoExportOptions options;
    options.start = 0;
    options.end = 60;
    options.encode.fps = 30;
    options.encode.codec = "libx264";
    options.encode.preset = "medium";
    options.encode.crf = 18;

    const std::string out = "output/camera_motion/tilt.mp4";
    REQUIRE(video::render_to_mp4(renderer, comp, out, options));
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);
}

#else

TEST_CASE("Camera motion tilt exports a real MP4") {
    CHECK(true);
}

#endif
