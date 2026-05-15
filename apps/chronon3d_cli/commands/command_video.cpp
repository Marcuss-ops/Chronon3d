#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <spdlog/spdlog.h>

#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/backends/video/video_export.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <Operations/background/dark_grid_background.hpp>

#include <algorithm>
#include <cctype>
#include <optional>

namespace chronon3d {
namespace cli {

namespace {

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::optional<animation::MotionAxis> parse_motion_axis(std::string axis) {
    axis = lower_copy(std::move(axis));
    if (axis == "tilt") return animation::MotionAxis::Tilt;
    if (axis == "pan") return animation::MotionAxis::Pan;
    if (axis == "roll") return animation::MotionAxis::Roll;
    return std::nullopt;
}

const char* axis_name(animation::MotionAxis axis) {
    switch (axis) {
    case animation::MotionAxis::Tilt: return "Tilt";
    case animation::MotionAxis::Pan: return "Pan";
    case animation::MotionAxis::Roll: return "Roll";
    }
    return "Tilt";
}

void build_camera_reference_content(SceneBuilder& s,
                                    const FrameContext& ctx,
                                    const animation::CameraMotionParams& p) {
    operations::background::dark_grid_background(s, ctx);

    const f32 inset_x = static_cast<f32>(ctx.width) * 0.06f;
    const f32 inset_y = static_cast<f32>(ctx.height) * 0.06f;
    const Vec2 image_size{
        static_cast<f32>(ctx.width) - inset_x * 2.0f,
        static_cast<f32>(ctx.height) - inset_y * 2.0f,
    };
    const Vec3 image_pos{
        static_cast<f32>(ctx.width) * 0.5f,
        static_cast<f32>(ctx.height) * 0.5f,
        0.0f,
    };

    s.layer("reference-image", [reference_image = p.reference_image, image_size, image_pos](LayerBuilder& l) {
        l.enable_3d()
         .image("grid_reference", {
             .path = reference_image,
             .size = image_size,
             .pos = image_pos,
             .opacity = 1.0f,
         });
    });
}

} // namespace

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (args.comp_id.empty()) {
        spdlog::error("Missing composition id for video export.");
        return 1;
    }
    if (args.output.empty()) {
        spdlog::error("Missing output path for video export.");
        return 1;
    }
    if (args.end <= args.start) {
        spdlog::error("Invalid export range: end ({}) must be greater than start ({})",
                      args.end, args.start);
        return 1;
    }

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    RenderSettings settings;
    settings.diagnostic = false;
    settings.use_modular_graph = args.use_modular_graph;
    settings.motion_blur.enabled = resolved.from_specscene ? false : args.motion_blur;
    settings.motion_blur.samples = args.motion_blur_samples;
    settings.motion_blur.shutter_angle = args.shutter_angle;
    settings.ssaa_factor = args.ssaa;

    auto renderer = create_renderer(registry, settings);

    video::VideoExportOptions options;
    options.start = args.start;
    options.end = args.end;
    options.encode.fps = args.fps;
    options.encode.crf = args.crf;
    options.encode.codec = args.codec;
    options.encode.preset = args.preset;

    video::FfmpegEncoder encoder;
    return video::render_to_video(*renderer, *resolved.comp, args.output, options, encoder) ? 0 : 1;
}

int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args) {
    auto axis = parse_motion_axis(args.axis);
    if (!axis) {
        spdlog::error("Unknown camera axis '{}'. Expected Tilt, Pan, or Roll.", args.axis);
        return 1;
    }

    VideoCameraArgs normalized = args;
    if (normalized.output.empty()) {
        normalized.output = "output/camera_" + lower_copy(axis_name(*axis)) + "_video.mp4";
    }

    if (normalized.end <= normalized.start) {
        spdlog::error("Invalid export range: end ({}) must be greater than start ({})",
                      normalized.end, normalized.start);
        return 1;
    }

    RenderSettings settings;
    settings.diagnostic = false;
    settings.use_modular_graph = normalized.use_modular_graph;
    settings.motion_blur.enabled = normalized.motion_blur;
    settings.motion_blur.samples = normalized.motion_blur_samples;
    settings.motion_blur.shutter_angle = normalized.shutter_angle;
    settings.ssaa_factor = normalized.ssaa;

    auto renderer = create_renderer(registry, settings);

    animation::CameraMotionParams params;
    params.duration = normalized.end - normalized.start;
    params.start_frame = normalized.start;
    params.reference_image = normalized.reference_image;

    const std::string comp_name = std::string("CameraTiltImage") + axis_name(*axis) + "Clip";
    auto comp = animation::camera_motion_clip(
        comp_name,
        *axis,
        params,
        [](SceneBuilder& s, const FrameContext& ctx, const animation::CameraMotionParams& p) {
            build_camera_reference_content(s, ctx, p);
        });

    video::VideoExportOptions options;
    options.start = normalized.start;
    options.end = normalized.end;
    options.encode.fps = normalized.fps;
    options.encode.crf = normalized.crf;
    options.encode.codec = normalized.codec;
    options.encode.preset = normalized.preset;

    video::FfmpegEncoder encoder;
    return video::render_to_video(*renderer, comp, normalized.output, options, encoder) ? 0 : 1;
}

} // namespace cli
} // namespace chronon3d

#else

namespace chronon3d::cli {
int command_video(const CompositionRegistry&, const VideoArgs&) {
    spdlog::error("Chronon3D was built without video support (CHRONON_WITH_VIDEO).");
    return 1;
}

int command_video_camera(const CompositionRegistry&, const VideoCameraArgs&) {
    spdlog::error("Chronon3D was built without video support (CHRONON_WITH_VIDEO).");
    return 1;
}
}

#endif
