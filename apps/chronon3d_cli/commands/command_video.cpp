#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <spdlog/spdlog.h>

#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/backends/video/video_export.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <Operations/background/dark_grid_background.hpp>
#include "video_camera_preset.hpp"

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

void apply_camera_cli_overrides(VideoCameraArgs& target, const VideoCameraArgs& cli) {
    const VideoCameraArgs defaults{};
    if (cli.axis != defaults.axis) target.axis = cli.axis;
    if (cli.reference_image != defaults.reference_image) target.reference_image = cli.reference_image;
    if (!cli.output.empty()) target.output = cli.output;
    if (cli.start != defaults.start) target.start = cli.start;
    if (cli.end != defaults.end) target.end = cli.end;
    if (cli.width != defaults.width) target.width = cli.width;
    if (cli.height != defaults.height) target.height = cli.height;
    if (cli.fps != defaults.fps) target.fps = cli.fps;
    if (cli.crf != defaults.crf) target.crf = cli.crf;
    if (cli.codec != defaults.codec) target.codec = cli.codec;
    if (cli.encode_preset != defaults.encode_preset) target.encode_preset = cli.encode_preset;
    if (cli.use_modular_graph != defaults.use_modular_graph) target.use_modular_graph = cli.use_modular_graph;
    if (cli.motion_blur != defaults.motion_blur) target.motion_blur = cli.motion_blur;
    if (cli.motion_blur_samples != defaults.motion_blur_samples) target.motion_blur_samples = cli.motion_blur_samples;
    if (cli.shutter_angle != defaults.shutter_angle) target.shutter_angle = cli.shutter_angle;
    if (cli.ssaa != defaults.ssaa) target.ssaa = cli.ssaa;
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
    options.encode.preset = args.encode_preset;

    video::FfmpegEncoder encoder;
    return video::render_to_video(*renderer, *resolved.comp, args.output, options, encoder) ? 0 : 1;
}

int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args) {
    VideoCameraArgs normalized = args;
    if (!normalized.profile.empty()) {
        std::string preset_error;
        std::filesystem::path preset_source;
        const auto preset = load_camera_preset(normalized.profile, &preset_source, &preset_error);
        if (!preset) {
            spdlog::error("Failed to load camera preset '{}': {}", normalized.profile, preset_error);
            return 1;
        }

        if (preset->axis) normalized.axis = *preset->axis;
        if (preset->reference_image) normalized.reference_image = *preset->reference_image;
        if (preset->output) normalized.output = *preset->output;
        if (preset->start) normalized.start = *preset->start;
        if (preset->end) normalized.end = *preset->end;
        if (preset->width) normalized.width = *preset->width;
        if (preset->height) normalized.height = *preset->height;
        if (preset->fps) normalized.fps = *preset->fps;
        if (preset->crf) normalized.crf = *preset->crf;
        if (preset->codec) normalized.codec = *preset->codec;
        if (preset->encode_preset) normalized.encode_preset = *preset->encode_preset;
        if (preset->use_modular_graph) normalized.use_modular_graph = *preset->use_modular_graph;
        if (preset->motion_blur) normalized.motion_blur = *preset->motion_blur;
        if (preset->motion_blur_samples) normalized.motion_blur_samples = *preset->motion_blur_samples;
        if (preset->shutter_angle) normalized.shutter_angle = *preset->shutter_angle;
        if (preset->ssaa) normalized.ssaa = *preset->ssaa;

        spdlog::info("Loaded camera preset '{}' from {}", normalized.profile, preset_source.string());
    }

    apply_camera_cli_overrides(normalized, args);

    auto axis = parse_motion_axis(normalized.axis);
    if (!axis) {
        spdlog::error("Unknown camera axis '{}'. Expected Tilt, Pan, or Roll.", normalized.axis);
        return 1;
    }

    if (normalized.output.empty()) {
        normalized.output = "output/camera_" + lower_copy(normalized.axis) + "_video.mp4";
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
    params.axis = *axis;
    params.duration = normalized.end - normalized.start;
    params.start_frame = normalized.start;
    params.reference_image = normalized.reference_image;

    auto comp = animation::camera_motion_clip(
        "CameraImageClip",
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
    options.encode.preset = normalized.encode_preset;

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
