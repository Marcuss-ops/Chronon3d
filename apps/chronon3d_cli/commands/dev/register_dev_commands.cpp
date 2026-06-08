#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../utils/common/cli_utils.hpp"
#include <memory>

namespace chronon3d::cli {

namespace {

struct DevState {
    std::shared_ptr<std::string> watch_id{std::make_shared<std::string>()};
    std::shared_ptr<std::string> output_dir{std::make_shared<std::string>(chronon_artifact_path("verify", "").string())};
};

struct BatchState {
    std::shared_ptr<std::vector<std::string>> jobs{std::make_shared<std::vector<std::string>>()};
};

struct ProofsState { std::shared_ptr<ProofsArgs> args{std::make_shared<ProofsArgs>()}; };
struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };

void register_watch(CLI::App& dev, CliContext& ctx) {
    auto watch_id = std::make_shared<std::string>();
    auto* watch = dev.add_subcommand("watch", "Watch for changes and re-render");
    watch->add_option("id", *watch_id, "Composition name")->required();
    watch->callback([watch_id, &ctx]() {
        ctx.exit_code = command_watch(ctx.registry, *watch_id);
    });
}

void register_render_all(CLI::App& dev, CliContext& ctx) {
    auto output_dir = std::make_shared<std::string>(chronon_artifact_path("verify", "").string());
    auto* render_all = dev.add_subcommand("render-all", "Render frame 0 of every registered composition");
    render_all->add_option("-o,--output-dir", *output_dir, "Output directory")->default_val(chronon_artifact_path("verify", "").string());
    render_all->callback([output_dir, &ctx]() {
        ctx.exit_code = command_verify(ctx.registry, *output_dir);
    });
}

void register_batch(CLI::App& dev, CliContext& ctx) {
    auto jobs = std::make_shared<std::vector<std::string>>();
    auto* cmd = dev.add_subcommand("batch", "Run multiple rendering jobs from explicit CLI job specs");
    cmd->add_option("--job", *jobs, "Job spec string: composition|frames|output|diagnostic|graph")->required();
    cmd->callback([jobs, &ctx]() {
        ctx.exit_code = command_batch(ctx.registry, *jobs);
    });
}

void register_proofs(CLI::App& dev, CliContext& ctx) {
    auto state = std::make_shared<ProofsState>();
    auto& args = *state->args;
    auto* cmd = dev.add_subcommand("proofs", "Render diagnostic proof compositions to PNG snapshots");
    cmd->add_option("-o,--out", args.output_dir, "Output directory")->default_val(chronon_artifact_path("proofs", "").string());
    cmd->add_option("--ssaa", args.ssaa, "Super sampling factor")->default_val(1.0f);
    cmd->callback([state, &ctx]() { ctx.exit_code = command_proofs(ctx.registry, *state->args); });
}

void register_studio_tools(CLI::App& dev, CliContext& ctx) {
    // preview
    {
        auto state = std::make_shared<RenderState>();
        auto& args = *state->args;
        auto* cmd = dev.add_subcommand("preview", "Render a single frame of a composition (default: middle frame)");
        cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
        cmd->add_option("--frame", args.frames, "Frame number to preview (default: middle frame)");
        cmd->add_option("-o,--output", args.output, "Output image path")->default_val(chronon_artifact_path("previews", "preview.png").string());
        cmd->add_flag("--diagnostic,--layout-preview", args.pipeline.diagnostic,
                      "Enable layout preview overlays (bbox, anchors, center guide)");
        cmd->add_flag("--graph,!--no-graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
        cmd->callback([state, &ctx]() {
            ctx.exit_code = command_preview(ctx.registry, *state->args);
        });
    }
    // contact-sheet
    {
        auto state = std::make_shared<RenderState>();
        auto& args = *state->args;
        auto* cmd = dev.add_subcommand("contact-sheet", "Render 4 frames stitched horizontally into a contact sheet");
        cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
        cmd->add_option("-o,--output", args.output, "Output image path")->default_val(chronon_artifact_path("previews", "contact_sheet.png").string());
        cmd->add_flag("--diagnostic,--layout-preview", args.pipeline.diagnostic,
                      "Enable layout preview overlays (bbox, anchors, center guide)");
        cmd->add_flag("--graph,!--no-graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
        cmd->callback([state, &ctx]() {
            ctx.exit_code = command_contact_sheet(ctx.registry, *state->args);
        });
    }
    // storyboard
    {
        auto state = std::make_shared<RenderState>();
        auto& args = *state->args;
        auto* cmd = dev.add_subcommand("storyboard", "Render 6 frames in a 2x3 grid with overlays");
        cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
        cmd->add_option("-o,--output", args.output, "Output image path")->default_val(chronon_artifact_path("previews", "storyboard.png").string());
        cmd->add_flag("--diagnostic,--layout-preview", args.pipeline.diagnostic,
                      "Enable layout preview overlays (bbox, anchors, center guide)");
        cmd->add_flag("--graph,!--no-graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
        cmd->callback([state, &ctx]() {
            ctx.exit_code = command_storyboard(ctx.registry, *state->args);
        });
    }
}

void register_bench_convert(CLI::App& dev, CliContext& ctx) {
    auto bc_args = std::make_shared<BenchConvertArgs>();
    auto* cmd = dev.add_subcommand("bench-convert", "Benchmark YUV frame conversion paths (HWY, TBB, sws_scale)");
    cmd->add_option("input", bc_args->comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frame", bc_args->frame, "Frame number to render")->default_val(0);
    cmd->add_option("--iterations", bc_args->iterations, "Number of conversion iterations per path")->default_val(10);
    cmd->add_option("--format", bc_args->format, "Output format: yuv420p or nv12")->default_val("yuv420p");
    cmd->add_flag("--gamma,!--no-gamma", bc_args->apply_gamma, "Apply sRGB gamma (default: on)");
    cmd->callback([bc_args, &ctx]() { ctx.exit_code = command_bench_convert(ctx.registry, *bc_args); });
}

void register_camera_video(CLI::App& dev, CliContext& ctx) {
    auto camera_args = std::make_shared<VideoCameraArgs>();
    auto* camera_cmd = dev.add_subcommand("camera-video", "Render the built-in camera reference clip");
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
    camera_cmd->add_option("--hardware", camera_args->hardware_encoder, "Hardware encoder: none, auto, nvenc, qsv, videotoolbox, amf")->default_val("none");
    camera_cmd->add_flag("--graph,!--no-graph", camera_args->pipeline.use_modular_graph, "Use modular RenderGraph path");
    camera_cmd->add_flag("--motion-blur", camera_args->pipeline.quality.motion_blur, "Enable temporal motion blur");
    camera_cmd->add_option("--ssaa", camera_args->pipeline.quality.ssaa, "Super Sampling factor");
    camera_cmd->callback([camera_args, &ctx]() { ctx.exit_code = command_video_camera(ctx.registry, *camera_args); });
}

} // namespace

void register_dev_commands(CLI::App& app, CliContext& ctx) {
    auto* dev = app.add_subcommand("dev", "Development and verification tools");
    register_watch(*dev, ctx);
    register_render_all(*dev, ctx);
    register_batch(*dev, ctx);
    register_proofs(*dev, ctx);
    register_studio_tools(*dev, ctx);
    register_bench_convert(*dev, ctx);
    register_camera_video(*dev, ctx);
}

} // namespace chronon3d::cli
