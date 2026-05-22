#include "../command_registry.hpp"
#include "../commands.hpp"
#include <CLI/Validators.hpp>
#include <CLI/ExtraValidators.hpp>
#include <memory>

namespace chronon3d::cli {

namespace {
struct VideoState { std::shared_ptr<VideoArgs> args{std::make_shared<VideoArgs>()}; };
}

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
    cmd->add_option("--hardware", args.hardware_encoder, "Hardware encoder: none, auto, nvenc, qsv, videotoolbox, amf")->default_val("none");
    cmd->add_flag("--keep-frames", args.keep_frames, "Keep temporary PNG frames");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--dirty-rects", args.pipeline.dirty_rects, "Enable dirty rectangles invalidation");
    cmd->add_flag("--motion-blur", args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.pipeline.quality.motion_blur_samples, "Subframe samples")->default_val(8);
    cmd->add_option("--shutter-angle", args.pipeline.quality.shutter_angle, "Shutter angle in degrees")->default_val(180.0f);
    cmd->add_option("--frames-dir", args.frames_dir, "Override temporary frames directory");
    cmd->add_option("--ssaa", args.pipeline.quality.ssaa, "Super Sampling factor")->default_val(1.0f);
    cmd->add_option("--chunks", args.chunks, "Render frame range in N parallel chunks before encoding")->default_val(1);
    cmd->add_option("--ffmpeg-mode", args.ffmpeg_mode, "Fallback FFmpeg mode: png, pipe")
        ->default_val("png")
        ->check(CLI::IsMember({"png", "pipe"}));
    cmd->add_flag("--ffmpeg-verbose", args.ffmpeg_verbose, "Show FFmpeg logs in pipe mode");
    cmd->add_option("--pipe-pixfmt", args.pipe_pixfmt, "Pixel format for raw pipe input: rgba, yuv420p, nv12")
        ->default_val("yuv420p")
        ->check(CLI::IsMember({"rgba", "yuv420p", "nv12"}));
    cmd->add_option("--color-output", args.color_output, "Output color space: srgb, rec709, linearsrgb")
        ->default_val("srgb")
        ->check(CLI::IsMember({"srgb", "rec709", "linearsrgb"}));
    cmd->add_flag("--warmup,--warmup-renderer", args.pipeline.warmup_renderer,
                  "Preallocate framebuffers and prime caches before rendering");
    cmd->add_option("--warmup-framebuffers", args.pipeline.warmup_framebuffers,
                    "Number of framebuffers to preallocate (default 16)");
    cmd->add_flag("--warmup-dummy-frame", args.pipeline.warmup_dummy_frame,
                  "Render a dummy frame 0 to prime all caches");
    cmd->callback([state, &ctx]() { ctx.exit_code = command_video(ctx.registry, *state->args); });
}

} // namespace chronon3d::cli
