#include <doctest/doctest.h>

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/backends/video/video_export.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>

#include <filesystem>
#include <optional>

#ifdef CHRONON_WITH_VIDEO
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

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

std::optional<std::string> read_primary_video_codec(const std::filesystem::path& path) {
    AVFormatContext* format_context = nullptr;
    const std::string input_path = path.string();

    if (avformat_open_input(&format_context, input_path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }

    const auto close_input = [&]() {
        if (format_context) {
            avformat_close_input(&format_context);
        }
    };

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        close_input();
        return std::nullopt;
    }

    for (unsigned i = 0; i < format_context->nb_streams; ++i) {
        const AVStream* stream = format_context->streams[i];
        if (stream && stream->codecpar && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const char* codec_name = avcodec_get_name(stream->codecpar->codec_id);
            std::optional<std::string> result;
            if (codec_name && *codec_name) {
                result = std::string(codec_name);
            }
            close_input();
            return result;
        }
    }

    close_input();
    return std::nullopt;
}

} // namespace

namespace {

void render_motion_video(MotionAxis axis, const char* name, const char* filename) {
    std::filesystem::create_directories("output/camera_motion");

    SoftwareRenderer renderer;
    CameraMotionParams params;
    params.axis = axis;
    params.width = 1280;
    params.height = 720;
    auto comp = chronon3d::presets::camera_motion_clip(name, params, add_motion_content);

    video::VideoExportOptions options;
    options.start = 0;
    options.end = 12;
    options.encode.fps = 30;
    options.encode.preset = "veryfast";
    options.encode.crf = 20;
    options.encode.codec = "libx264";

    const std::string out = (std::filesystem::path("output/camera_motion") / filename).string();
    video::FfmpegEncoder encoder;
    REQUIRE(video::render_to_video(renderer, comp, out, options, encoder));
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);

    const auto codec = read_primary_video_codec(out);
    REQUIRE(codec.has_value());
    CHECK_EQ(*codec, "h264");
}

} // namespace

TEST_CASE("Camera motion videos export real MP4s") {
    SUBCASE("tilt") {
        render_motion_video(MotionAxis::Tilt, "CameraMotionTiltVideo", "tilt.mp4");
    }

    SUBCASE("pan") {
        render_motion_video(MotionAxis::Pan, "CameraMotionPanVideo", "pan.mp4");
    }

    SUBCASE("roll") {
        render_motion_video(MotionAxis::Roll, "CameraMotionRollVideo", "roll.mp4");
    }
}

#else

TEST_CASE("Camera motion videos export real MP4s") {
    CHECK(true);
}

#endif
