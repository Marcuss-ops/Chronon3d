#include <chronon3d/chronon3d.hpp>
#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <spdlog/spdlog.h>
#include "commands.hpp"

using namespace chronon3d;
using namespace chronon3d::cli;

int main(int argc, char** argv) {
    CLI::App app{"Chronon3d CLI - Motion Graphics Engine"};
    app.require_subcommand(1);

    CompositionRegistry registry;

    int exit_code = 0;

    // -------------------------------------------------------------------------
    // list
    // -------------------------------------------------------------------------
    auto* list_cmd = app.add_subcommand("list", "List all registered compositions");
    list_cmd->callback([&]() {
        exit_code = command_list(registry);
    });

    // -------------------------------------------------------------------------
    // info
    // -------------------------------------------------------------------------
    std::string info_id;
    auto* info_cmd = app.add_subcommand("info", "Get information about a composition");
    info_cmd->add_option("id", info_id, "Composition name")->required();
    info_cmd->callback([&]() {
        exit_code = command_info(registry, info_id);
    });

    // -------------------------------------------------------------------------
    // doctor
    // -------------------------------------------------------------------------
    auto* doctor_cmd = app.add_subcommand("doctor", "Check whether the local Chronon3d environment is ready");
    doctor_cmd->callback([&]() {
        exit_code = command_doctor(registry);
    });

    // -------------------------------------------------------------------------
    // verify
    // -------------------------------------------------------------------------
    std::string verify_output_dir = "output/verify";
    auto* verify_cmd = app.add_subcommand("verify", "Run a quick render and video smoke test");
    verify_cmd->add_option("-o,--output-dir", verify_output_dir, "Output directory")->default_val("output/verify");
    verify_cmd->callback([&]() {
        exit_code = command_verify(registry, verify_output_dir);
    });

    // -------------------------------------------------------------------------
    // render
    // -------------------------------------------------------------------------
    RenderArgs render_args;
    auto* render_cmd = app.add_subcommand("render", "Render a composition id or .specscene file to image(s)");
    render_cmd->add_option("input",      render_args.comp_id,   "Composition name or .specscene path")->required();
    render_cmd->add_option("--frames",   render_args.frames,    "Frame range: 0 | 0-90 | 0-90x5");
    render_cmd->add_option("-o,--output",render_args.output,    "Output path (use #### for frame number)");
    render_cmd->add_flag("--diagnostic",            render_args.diagnostic,          "Enable diagnostic overlays");
    render_cmd->add_flag("--graph",                 render_args.use_modular_graph,   "Use modular RenderGraph path");
    render_cmd->add_flag("--motion-blur",           render_args.motion_blur,         "Enable temporal motion blur");
    render_cmd->add_option("--motion-blur-samples", render_args.motion_blur_samples, "Subframe samples (default 8)");
    render_cmd->add_option("--shutter-angle",       render_args.shutter_angle,       "Shutter angle in degrees (default 180)");
    render_cmd->add_option("--frame",               render_args.frame_old,           "Single frame (legacy)");
    render_cmd->add_option("--start",               render_args.start_old,           "Start frame (legacy)");
    render_cmd->add_option("--end",                 render_args.end_old,             "End frame exclusive (legacy)");
    render_cmd->add_option("--ssaa",                render_args.ssaa,                "Super Sampling factor (default 1.0)");
    render_cmd->callback([&]() {
        if (render_args.output.empty()) {
            render_args.output = "render_####.png";
            spdlog::warn("No output path specified, defaulting to {}", render_args.output);
        }
        exit_code = command_render(registry, render_args);
    });

#ifdef CHRONON_WITH_VIDEO
    // -------------------------------------------------------------------------
    // video
    // -------------------------------------------------------------------------
    VideoArgs video_args;
    auto* video_cmd = app.add_subcommand("video", "Render a composition to MP4 via ffmpeg");
    video_cmd->add_option("id",           video_args.comp_id,     "Composition name or .specscene path");
    video_cmd->add_option("-o,--output",  video_args.output,      "Output .mp4 path");
    video_cmd->add_option("--start",      video_args.start,       "Start frame (inclusive)");
    video_cmd->add_option("--end",        video_args.end,         "End frame (exclusive)");
    video_cmd->add_option("--fps",        video_args.fps,         "Output frame rate");
    video_cmd->add_option("--crf",        video_args.crf,         "x264 CRF (0-51, lower=better)");
    video_cmd->add_option("--codec",      video_args.codec,       "Video encoder (auto, libx264, mpeg4, etc.)");
    video_cmd->add_option("--encode-preset,--preset", video_args.encode_preset, "x264 preset");
    video_cmd->add_flag("--keep-frames",            video_args.keep_frames,          "Keep temporary PNG frames");
    video_cmd->add_flag("--graph",                  video_args.use_modular_graph,    "Use modular RenderGraph path");
    video_cmd->add_flag("--motion-blur",            video_args.motion_blur,          "Enable temporal motion blur");
    video_cmd->add_option("--motion-blur-samples",  video_args.motion_blur_samples,  "Subframe samples (default 8)");
    video_cmd->add_option("--shutter-angle",        video_args.shutter_angle,        "Shutter angle in degrees (default 180)");
    video_cmd->add_option("--frames-dir",           video_args.frames_dir,           "Override temporary frames directory");
    video_cmd->add_option("--ssaa",                 video_args.ssaa,                 "Super Sampling factor (default 1.0)");
    video_cmd->callback([&]() {
        if (!video_cmd->get_subcommands().empty()) {
            return;
        }
        exit_code = command_video(registry, video_args);
    });

    VideoCameraArgs camera_args;
    auto* camera_cmd = video_cmd->add_subcommand("camera", "Render the built-in camera reference clip");
    camera_cmd->add_option("--axis",     camera_args.axis,          "Camera axis: Tilt, Pan, or Roll");
    camera_cmd->add_option("--reference", camera_args.reference_image, "Reference image path");
    camera_cmd->add_option("-o,--output", camera_args.output,       "Output .mp4 path");
    camera_cmd->add_option("--start",    camera_args.start,         "Start frame (inclusive)");
    camera_cmd->add_option("--end",      camera_args.end,           "End frame (exclusive)");
    camera_cmd->add_option("--roll-start", camera_args.roll_start_deg, "Roll start angle in degrees (Roll axis only)");
    camera_cmd->add_option("--roll-end",   camera_args.roll_end_deg,   "Roll end angle in degrees (Roll axis only)");
    camera_cmd->add_option("--fps",      camera_args.fps,           "Output frame rate");
    camera_cmd->add_option("--crf",      camera_args.crf,           "x264 CRF (0-51, lower=better)");
    camera_cmd->add_option("--codec",    camera_args.codec,         "Video encoder (auto, libx264, mpeg4, etc.)");
    camera_cmd->add_option("--encode-preset", camera_args.encode_preset, "x264 preset");
    camera_cmd->add_flag("--graph",                  camera_args.use_modular_graph,    "Use modular RenderGraph path");
    camera_cmd->add_flag("--motion-blur",            camera_args.motion_blur,          "Enable temporal motion blur");
    camera_cmd->add_option("--motion-blur-samples",  camera_args.motion_blur_samples,  "Subframe samples (default 8)");
    camera_cmd->add_option("--shutter-angle",        camera_args.shutter_angle,        "Shutter angle in degrees (default 180)");
    camera_cmd->add_option("--ssaa",                 camera_args.ssaa,                 "Super Sampling factor (default 1.0)");
    camera_cmd->callback([&]() {
        exit_code = command_video_camera(registry, camera_args);
    });
#endif

    // -------------------------------------------------------------------------
    // bench
    // -------------------------------------------------------------------------
    BenchArgs bench_args;
    auto* bench_cmd = app.add_subcommand("bench", "Benchmark a composition in-memory");
    bench_cmd->add_option("id", bench_args.comp_id, "Composition name")->required();
    bench_cmd->add_option("--frames", bench_args.frames, "Measured frames")->default_val(120);
    bench_cmd->add_option("--warmup", bench_args.warmup, "Warmup frames")->default_val(10);
    bench_cmd->add_flag("--graph", bench_args.use_modular_graph, "Use modular RenderGraph path");
    bench_cmd->callback([&]() {
        exit_code = command_bench(registry, bench_args);
    });

    // -------------------------------------------------------------------------
    // graph
    // -------------------------------------------------------------------------
    GraphArgs graph_args;
    auto* graph_cmd = app.add_subcommand("graph", "Export the render graph as DOT");
    graph_cmd->add_option("id", graph_args.comp_id, "Composition name")->required();
    graph_cmd->add_option("--frame", graph_args.frame, "Frame to inspect")->default_val(0);
    graph_cmd->add_option("-o,--output", graph_args.output, "Output .dot path");
    graph_cmd->callback([&]() {
        if (graph_args.output.empty()) {
            graph_args.output = "output/graph.dot";
            spdlog::warn("No output path specified, defaulting to {}", graph_args.output);
        }
        exit_code = command_graph(registry, graph_args);
    });

    // -------------------------------------------------------------------------
    // batch
    // -------------------------------------------------------------------------
    std::vector<std::string> batch_jobs;
    auto* batch_cmd = app.add_subcommand("batch", "Run multiple rendering jobs from explicit CLI job specs");
    batch_cmd->add_option("--job", batch_jobs, "Job spec: composition|frames|output|diagnostic|graph")->expected(-1);
    batch_cmd->callback([&]() {
        exit_code = command_batch(registry, batch_jobs);
    });

    // -------------------------------------------------------------------------
    // dev subcommands
    // -------------------------------------------------------------------------
    auto* dev_cmd = app.add_subcommand("dev", "Development and verification tools");

    // dev watch
    std::string watch_id;
    auto* watch_cmd = dev_cmd->add_subcommand("watch", "Watch for changes and re-render");
    watch_cmd->add_option("id", watch_id, "Composition name")->required();
    watch_cmd->callback([&]() {
        exit_code = command_watch(registry, watch_id);
    });

    // dev render-all
    std::string all_output_dir;
    auto* all_cmd = dev_cmd->add_subcommand("render-all", "Render frame 0 of every registered composition");
    all_cmd->add_option("-o,--output-dir", all_output_dir, "Output directory")->default_val("output/verify");
    all_cmd->callback([&]() {
        for (const auto& id : registry.available()) {
            RenderArgs args;
            args.comp_id = id;
            args.frames  = "0";
            args.output  = all_output_dir + "/" + id + ".png";
            if (command_render(registry, args) != 0) exit_code = 1;
        }
    });

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return exit_code;
}
