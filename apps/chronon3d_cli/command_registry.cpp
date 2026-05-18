#include "command_registry.hpp"

#include "commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {

struct ListState {};
struct InfoState { std::shared_ptr<std::string> id{std::make_shared<std::string>()}; };
struct VerifyState { std::shared_ptr<std::string> output_dir{std::make_shared<std::string>("output/verify")}; };
struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };
struct VideoState { std::shared_ptr<VideoArgs> args{std::make_shared<VideoArgs>()}; };
struct BenchState { std::shared_ptr<BenchArgs> args{std::make_shared<BenchArgs>()}; };
struct GraphState { std::shared_ptr<GraphArgs> args{std::make_shared<GraphArgs>()}; };
struct ProofsState { std::shared_ptr<ProofsArgs> args{std::make_shared<ProofsArgs>()}; };
struct BatchState { std::shared_ptr<std::vector<std::string>> jobs{std::make_shared<std::vector<std::string>>()}; };
struct DevState { std::shared_ptr<std::string> watch_id{std::make_shared<std::string>()}; std::shared_ptr<std::string> output_dir{std::make_shared<std::string>("output/verify")}; };

void register_list(CLI::App& app, CliContext& ctx) {
    auto* cmd = app.add_subcommand("list", "List all registered compositions");
    cmd->callback([&ctx]() { ctx.exit_code = command_list(ctx.registry); });
}

void register_info(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<InfoState>();
    auto* cmd = app.add_subcommand("info", "Get information about a composition");
    cmd->add_option("id", *state->id, "Composition name")->required();
    cmd->callback([state, &ctx]() { ctx.exit_code = command_info(ctx.registry, *state->id); });
}

void register_doctor(CLI::App& app, CliContext& ctx) {
    auto* cmd = app.add_subcommand("doctor", "Check whether the local Chronon3d environment is ready");
    cmd->callback([&ctx]() { ctx.exit_code = command_doctor(ctx.registry); });
}

void register_verify(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<VerifyState>();
    auto* cmd = app.add_subcommand("verify", "Run a quick render and video smoke test");
    cmd->add_option("-o,--output-dir", *state->output_dir, "Output directory")->default_val("output/verify");
    cmd->callback([state, &ctx]() { ctx.exit_code = command_verify(ctx.registry, *state->output_dir); });
}

void register_render(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand("render", "Render a composition id or .specscene file to image(s)");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_flag("--diagnostic", args.diagnostic, "Enable diagnostic overlays");
    cmd->add_flag("--graph", args.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--motion-blur", args.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.motion_blur_samples, "Subframe samples (default 8)");
    cmd->add_option("--shutter-angle", args.shutter_angle, "Shutter angle in degrees (default 180)");
    cmd->add_option("--ssaa", args.ssaa, "Super Sampling factor (default 1.0)");
    cmd->callback([state, &ctx]() {
        if (state->args->output.empty()) {
            state->args->output = "render_####.png";
            spdlog::warn("No output path specified, defaulting to {}", state->args->output);
        }
        ctx.exit_code = command_render(ctx.registry, *state->args);
    });
}

void register_video(CLI::App& app, CliContext& ctx) {
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
    cmd->add_flag("--graph", args.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--motion-blur", args.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.motion_blur_samples, "Subframe samples")->default_val(8);
    cmd->add_option("--shutter-angle", args.shutter_angle, "Shutter angle in degrees")->default_val(180.0f);
    cmd->add_option("--frames-dir", args.frames_dir, "Override temporary frames directory");
    cmd->add_option("--ssaa", args.ssaa, "Super Sampling factor")->default_val(1.0f);
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
    camera_cmd->add_flag("--graph", camera_args->use_modular_graph, "Use modular RenderGraph path");
    camera_cmd->add_flag("--motion-blur", camera_args->motion_blur, "Enable temporal motion blur");
    camera_cmd->add_option("--ssaa", camera_args->ssaa, "Super Sampling factor");
    camera_cmd->callback([camera_args, &ctx]() { ctx.exit_code = command_video_camera(ctx.registry, *camera_args); });
#endif
}

void register_bench(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<BenchState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("bench", "Benchmark a composition in-memory");
    cmd->add_option("id", args.comp_id, "Composition name")->required();
    cmd->add_option("--frames", args.frames, "Measured frames")->default_val(120);
    cmd->add_option("--warmup", args.warmup, "Warmup frames")->default_val(10);
    cmd->add_flag("--graph", args.use_modular_graph, "Use modular RenderGraph path");
    cmd->callback([state, &ctx]() { ctx.exit_code = command_bench(ctx.registry, *state->args); });
}

void register_graph(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<GraphState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("graph", "Inspect the render graph: summary stats or DOT export");
    cmd->add_option("id", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frame", args.frame, "Frame to inspect")->default_val(0);
    cmd->add_option("-o,--output", args.output, "Output .dot file path");
    cmd->add_flag("--summary", args.summary, "Print node counts, cache stats, and timing");
    cmd->callback([state, &ctx]() {
        if (!state->args->summary && state->args->output.empty()) {
            state->args->output = "output/graph.dot";
            spdlog::warn("No --output and no --summary; defaulting to {}", state->args->output);
        }
        ctx.exit_code = command_graph(ctx.registry, *state->args);
    });
}

void register_proofs(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ProofsState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("proofs", "Render all 7 diagnostic proof compositions to PNG snapshots");
    cmd->add_option("-o,--out", args.output_dir, "Output directory")->default_val("output/proofs");
    cmd->add_option("--ssaa", args.ssaa, "Super sampling factor")->default_val(1.0f);
    cmd->callback([state, &ctx]() { ctx.exit_code = command_proofs(ctx.registry, *state->args); });
}

void register_batch(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<BatchState>();
    auto* cmd = app.add_subcommand("batch", "Run multiple rendering jobs from explicit CLI job specs");
    cmd->add_option("--job", *state->jobs, "Job spec: composition|frames|output|diagnostic|graph")->expected(-1);
    cmd->callback([state, &ctx]() { ctx.exit_code = command_batch(ctx.registry, *state->jobs); });
}

void register_dev(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<DevState>();
    auto* dev = app.add_subcommand("dev", "Development and verification tools");

    auto* watch = dev->add_subcommand("watch", "Watch for changes and re-render");
    watch->add_option("id", *state->watch_id, "Composition name")->required();
    watch->callback([state, &ctx]() { ctx.exit_code = command_watch(ctx.registry, *state->watch_id); });

    auto* render_all = dev->add_subcommand("render-all", "Render frame 0 of every registered composition");
    render_all->add_option("-o,--output-dir", *state->output_dir, "Output directory")->default_val("output/verify");
    render_all->callback([state, &ctx]() {
        for (const auto& id : ctx.registry.available()) {
            RenderArgs args;
            args.comp_id = id;
            args.frames = "0";
            args.output = *state->output_dir + "/" + id + ".png";
            if (command_render(ctx.registry, args) != 0) {
                ctx.exit_code = 1;
            }
        }
    });
}

} // namespace

void register_command(CLI::App& app, const CliCommandDescriptor& descriptor, CliContext& ctx) {
    descriptor.register_fn(app, ctx);
}

void register_all_commands(CLI::App& app, CliContext& ctx) {
    const CliCommandDescriptor commands[] = {
        {"list", "List all registered compositions", register_list},
        {"info", "Get information about a composition", register_info},
        {"doctor", "Check whether the local Chronon3d environment is ready", register_doctor},
        {"verify", "Run a quick render and video smoke test", register_verify},
        {"render", "Render a composition id or .specscene file to image(s)", register_render},
        {"video", "Render a composition to MP4 via ffmpeg", register_video},
        {"bench", "Benchmark a composition in-memory", register_bench},
        {"graph", "Inspect the render graph: summary stats or DOT export", register_graph},
        {"proofs", "Render all 7 diagnostic proof compositions to PNG snapshots", register_proofs},
        {"batch", "Run multiple rendering jobs from explicit CLI job specs", register_batch},
        {"dev", "Development and verification tools", register_dev},
    };

    for (const auto& cmd : commands) {
        register_command(app, cmd, ctx);
    }
}

} // namespace chronon3d::cli
