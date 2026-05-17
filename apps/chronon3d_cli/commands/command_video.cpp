#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include "../utils/cli_mappers.hpp"
#include <spdlog/spdlog.h>

#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/backends/video/video_export.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>

namespace chronon3d::cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    auto renderer = create_renderer(registry, settings_from_args(args, !resolved.from_specscene));
    
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
    auto axis = parse_motion_axis(args.axis);
    if (!axis) {
        spdlog::error("Unknown camera axis '{}'. Expected Tilt, Pan, or Roll.", args.axis);
        return 1;
    }

    std::string output = args.output.empty() ? 
        "output/camera_" + lower_copy(args.axis) + "_video.mp4" : args.output;

    auto renderer = create_renderer(registry, settings_from_args(args));

    animation::CameraMotionParams params;
    params.axis = *axis;
    if (*axis == animation::MotionAxis::Roll) {
        params.start_deg = args.roll_start_deg;
        params.end_deg = args.roll_end_deg;
    }
    params.duration = args.end - args.start;
    params.start_frame = args.start;
    params.width = args.width;
    params.height = args.height;
    params.reference_image = args.reference_image;
    params.pose.position = params.position;
    params.pose.zoom = params.zoom;

    auto comp = chronon3d::presets::camera_motion_clip("CameraTestPattern", params,
        [](SceneBuilder& s, const FrameContext& ctx, const animation::CameraMotionParams& p) {
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

            scene::utils::dark_grid_background(s, ctx);

            s.layer("reference-image", [reference_image = p.reference_image, image_size, image_pos](LayerBuilder& l) {
                l.enable_3d()
                 .image("grid_reference", {
                     .path = reference_image,
                     .size = image_size,
                     .pos = image_pos,
                     .opacity = 1.0f,
                 });
            });
        });

    video::VideoExportOptions options;
    options.start = args.start;
    options.end = args.end;
    options.encode.fps = args.fps;
    options.encode.crf = args.crf;
    options.encode.codec = args.codec;
    options.encode.preset = args.encode_preset;

    video::FfmpegEncoder encoder;
    return video::render_to_video(*renderer, comp, output, options, encoder) ? 0 : 1;
}

} // namespace chronon3d::cli

#else

// ── System-ffmpeg fallback (no SDK required, needs ffmpeg in PATH) ─────────

#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <fmt/format.h>
#include <filesystem>
#include <cstdlib>

namespace chronon3d::cli {

namespace {
bool ffmpeg_in_path() {
#ifdef _WIN32
    return std::system("ffmpeg -version > NUL 2>&1") == 0;
#else
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
#endif
}
} // namespace

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (args.comp_id.empty()) {
        spdlog::error("[video] No composition specified.");
        return 1;
    }
    if (args.output.empty()) {
        spdlog::error("[video] No output path specified (-o/--output).");
        return 1;
    }
    if (!ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH. Install FFmpeg or rebuild with CHRONON3D_ENABLE_VIDEO=ON.");
        return 1;
    }

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;
    const auto& comp = *resolved.comp;

    RenderSettings settings;
    settings.use_modular_graph = args.use_modular_graph;
    settings.motion_blur.enabled = args.motion_blur;
    settings.motion_blur.samples = args.motion_blur_samples;
    settings.motion_blur.shutter_angle = args.shutter_angle;
    settings.ssaa_factor = args.ssaa;
    auto renderer = create_renderer(registry, settings);

    const Frame start = args.start;
    const Frame end   = (args.end > args.start) ? args.end : comp.duration();
    if (end <= start) {
        spdlog::error("[video] Empty frame range [{}, {})", start, end);
        return 1;
    }

    // Temporary frames directory
    const std::filesystem::path frames_dir = args.frames_dir.empty()
        ? std::filesystem::temp_directory_path() / ("chronon_" + args.comp_id)
        : std::filesystem::path(args.frames_dir);
    std::error_code ec;
    std::filesystem::create_directories(frames_dir, ec);
    if (ec) {
        spdlog::error("[video] Cannot create frames dir {}: {}", frames_dir.string(), ec.message());
        return 1;
    }

    spdlog::info("[video] Rendering {} frames [{}, {}) at {} fps → {}",
                 end - start, start, end, args.fps, args.output);

    for (Frame f = start; f < end; ++f) {
        auto fb = renderer->render_frame(comp, f);
        if (!fb) {
            spdlog::error("[video] Render failed at frame {}", f);
            return 1;
        }
        const auto png_path = (frames_dir / fmt::format("frame_{:06d}.png", f - start)).string();
        if (!save_png(*fb, png_path)) {
            spdlog::error("[video] PNG write failed: {}", png_path);
            return 1;
        }
        if ((f - start) % 30 == 0)
            spdlog::info("[video]   frame {}/{}", f - start, end - start);
    }

    std::filesystem::create_directories(
        std::filesystem::path(args.output).parent_path(), ec);

    const std::string pattern = (frames_dir / "frame_%06d.png").string();
    const std::string codec   = (args.codec == "auto") ? "libx264" : args.codec;
    const std::string cmd     = fmt::format(
        "ffmpeg -y -framerate {} -i \"{}\" -c:v {} -crf {} -preset {} -pix_fmt yuv420p \"{}\"",
        args.fps, pattern, codec, args.crf, args.encode_preset, args.output);

    spdlog::info("[video] {}", cmd);
    const int rc = std::system(cmd.c_str());

    if (!args.keep_frames) {
        std::filesystem::remove_all(frames_dir, ec);
    }

    if (rc != 0) {
        spdlog::error("[video] ffmpeg exited with code {}", rc);
        return 1;
    }
    spdlog::info("[video] Done → {}", args.output);
    return 0;
}

int command_video_camera(const CompositionRegistry&, const VideoCameraArgs&) {
    spdlog::error("[video] camera requires CHRONON3D_ENABLE_VIDEO=ON (FFmpeg SDK).");
    return 1;
}

} // namespace chronon3d::cli

#endif
