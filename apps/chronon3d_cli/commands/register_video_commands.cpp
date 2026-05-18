#include "../command_registry.hpp"
#include "../commands.hpp"
#include <memory>

namespace chronon3d::cli {

namespace {

struct VideoState { std::shared_ptr<VideoArgs> args{std::make_shared<VideoArgs>()}; };

} // namespace

void register_video_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<VideoState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand("video", "Render a composition to MP4 via ffmpeg");
    cmd->add_option("id", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("-o,--output", args.output, "Output .mp4 path")->required();
    cmd->add_option("--start", args.start, "Start frame (inclusive, default 0)");
    cmd->add_option("--end", args.end, "End frame exclusive (default: composition duration)");
    cmd->add_option("--fps", args.fps, "Output frame rate")->default_val(30);
    cmd->add_option("--crf", args.crf, "x264 CRF (0-51, lower=better)")->default_val(18);
    cmd->add_option("--codec", args.codec, "Video encoder (auto, libx264, mpeg4)")->default_val("auto");
    cmd->add_option("--encode-preset,--preset", args.encode_preset, "x264 preset")->default_val("medium");
    cmd->add_flag("--keep-frames", args.keep_frames, "Keep temporary PNG frames");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--motion-blur", args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.pipeline.quality.motion_blur_samples, "Subframe samples")->default_val(8);
    cmd->add_option("--shutter-angle", args.pipeline.quality.shutter_angle, "Shutter angle in degrees")->default_val(180.0f);
    cmd->add_option("--frames-dir", args.frames_dir, "Override temporary frames directory");
    cmd->add_option("--ssaa", args.pipeline.quality.ssaa, "Super Sampling factor")->default_val(1.0f);
    cmd->callback([state, &ctx]() { ctx.exit_code = command_video(ctx.registry, *state->args); });

#ifdef CHRONON_WITH_VIDEO
    auto camera_args = std::make_shared<VideoCameraArgs>();
    auto* camera_cmd = cmd->add_subcommand("camera", "Render the built-in camera reference clip");
    camera_cmd->add_option("--axis", camera_args->axis, "Camera axis: Tilt, Pan, or Roll");
    camera_cmd->add_option("--reference", camera_args->reference_image, "Reference image path");
    camera_cmd->add_option("-o,--output", camera_args->output, "Output .mp4 path");
    camera_cmd->add_option("--start", camera_args->start, "Start frame (inclusive)");
    camera_cmd->add_option("--end", camera_args->end, "End frame (exclusive)");
    camera_cmd->add_option("--roll-start", camera_args->roll_start_deg, "Roll start angle in degrees");
    camera_cmd->add_option("--roll-end", camera_args->roll_end_deg, "Roll end angle in degrees");
    camera_cmd->add_option("--fps", camera_args->fps, "Output frame rate");
    camera_cmd->add_option("--crf", camera_args->crf, "x264 CRF (0-51, lower=better)");
    camera_cmd->add_option("--codec", camera_args->codec, "Video encoder");
    camera_cmd->add_option("--encode-preset", camera_args->encode_preset, "x264 preset");
    camera_cmd->add_flag("--graph", camera_args->pipeline.use_modular_graph, "Use modular RenderGraph path");
    camera_cmd->add_flag("--motion-blur", camera_args->pipeline.quality.motion_blur, "Enable temporal motion blur");
    camera_cmd->add_option("--ssaa", camera_args->pipeline.quality.ssaa, "Super Sampling factor");
    camera_cmd->callback([camera_args, &ctx]() { ctx.exit_code = command_video_camera(ctx.registry, *camera_args); });
#endif
}

} // namespace chronon3d::cli
