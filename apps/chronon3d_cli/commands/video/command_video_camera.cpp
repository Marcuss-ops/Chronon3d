// ---------------------------------------------------------------------------
// command_video_camera.cpp — Video camera motion command
//
// Extracted from command_video.cpp (Item 18 step 6).
// ---------------------------------------------------------------------------

#include "common/video_export_common.hpp"
#include "../../utils/common/cli_utils.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include "../../utils/video/video_job_plan.hpp"
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_video_camera(const CompositionRegistry& registry,
                         const VideoCameraArgs& args)
{
    auto axis = parse_motion_axis(args.axis);
    if (!axis) {
        spdlog::error("Unknown camera axis '{}'. Expected Tilt, Pan, or Roll.",
                      args.axis);
        return 1;
    }

    std::string output = args.output.empty()
        ? chronon_artifact_path("videos",
            "camera_" + lower_copy(args.axis) + "_video.mp4").string()
        : args.output;

    RenderSettings settings = settings_from_args(args);

    animation::CameraMotionParams params;
    params.axis = *axis;
    if (*axis == animation::MotionAxis::Roll) {
        params.start_deg = args.roll_start_deg;
        params.end_deg   = args.roll_end_deg;
    }
    // CLI end is inclusive; convert to exclusive for internal use.
    // comp isn't defined yet (created below via camera_motion_clip),
    // so use args.end as fallback for the no-`--end` case (same as original).
    const Frame cam_end = (args.end > args.start) ? (args.end + 1) : args.end;
    params.duration       = cam_end - args.start;
    params.start_frame    = args.start;
    params.width          = args.width;
    params.height         = args.height;
    params.reference_image = args.reference_image;
    params.pose.position  = params.position;
    params.pose.zoom      = params.zoom;

    auto comp = chronon3d::presets::camera_motion_clip("CameraTestPattern", params,
        [](SceneBuilder& s, const FrameContext& ctx,
           const animation::CameraMotionParams& p) {
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

            s.layer("reference-image",
                [reference_image = p.reference_image, image_size, image_pos](
                    LayerBuilder& l) {
                    l.enable_3d()
                     .image("grid_reference", {
                         .path    = reference_image,
                         .size    = image_size,
                         .pos     = image_pos,
                         .opacity = 1.0f,
                     });
                });
        });

    FfmpegExportOptions opts;
    opts.output.output          = output;
    opts.output.frames_dir_name = "chronon_camera_" + lower_copy(args.axis);
    opts.output.fps             = args.fps;
    opts.encoder.crf            = args.crf;
    opts.encoder.codec          = args.codec;
    opts.encoder.hardware_encoder = args.hardware_encoder;
    opts.encoder.encode_preset  = args.encode_preset;
    opts.encoder.tune           = args.tune;
    opts.sink.keep_frames       = false; // default for camera motion
    opts.sink.chunks            = 1;
    opts.sink.ffmpeg_mode       = "png";
    opts.pipe.ffmpeg_verbose    = false;

    return render_and_encode_ffmpeg(registry, comp, comp.name(),
                                    settings, args.start, cam_end, opts);
}

} // namespace chronon3d::cli
