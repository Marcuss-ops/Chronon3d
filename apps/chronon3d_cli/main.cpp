#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include "commands.hpp"

using namespace chronon3d;
using namespace chronon3d::cli;

int main(int argc, char** argv) {
    CLI::App app{"Chronon3d CLI - Motion Graphics Engine"};
    app.require_subcommand(1);

    CompositionRegistry registry;
    register_all_compositions(registry);

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
    info_cmd->callback([&]() { exit_code = command_info(registry, info_id); });

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
    render_cmd->callback([&]() { exit_code = command_render(registry, render_args); });

    // -------------------------------------------------------------------------
    // video
    // -------------------------------------------------------------------------
    VideoArgs video_args;
    auto* video_cmd = app.add_subcommand("video", "Render a composition to MP4 via ffmpeg");
    video_cmd->add_option("id",           video_args.comp_id,     "Composition name")->required();
    video_cmd->add_option("-o,--output",  video_args.output,      "Output .mp4 path")->required();
    video_cmd->add_option("--start",      video_args.start,       "Start frame (inclusive)");
    video_cmd->add_option("--end",        video_args.end,         "End frame (exclusive)")->required();
    video_cmd->add_option("--fps",        video_args.fps,         "Output frame rate");
    video_cmd->add_option("--crf",        video_args.crf,         "x264 CRF (0-51, lower=better)");
    video_cmd->add_option("--preset",     video_args.preset,      "x264 preset");
    video_cmd->add_flag("--keep-frames",            video_args.keep_frames,          "Keep temporary PNG frames");
    video_cmd->add_flag("--graph",                  video_args.use_modular_graph,    "Use modular RenderGraph path");
    video_cmd->add_flag("--motion-blur",            video_args.motion_blur,          "Enable temporal motion blur");
    video_cmd->add_option("--motion-blur-samples",  video_args.motion_blur_samples,  "Subframe samples (default 8)");
    video_cmd->add_option("--shutter-angle",        video_args.shutter_angle,        "Shutter angle in degrees (default 180)");
    video_cmd->add_option("--frames-dir",           video_args.frames_dir,           "Override temporary frames directory");
    video_cmd->add_option("--ssaa",                 video_args.ssaa,                 "Super Sampling factor (default 1.0)");
    video_cmd->callback([&]() { exit_code = command_video(registry, video_args); });

    // -------------------------------------------------------------------------
    // bench
    // -------------------------------------------------------------------------
    BenchArgs bench_args;
    auto* bench_cmd = app.add_subcommand("bench", "Benchmark a composition in-memory");
    bench_cmd->add_option("id", bench_args.comp_id, "Composition name")->required();
    bench_cmd->add_option("--frames", bench_args.frames, "Measured frames")->default_val(120);
    bench_cmd->add_option("--warmup", bench_args.warmup, "Warmup frames")->default_val(10);
    bench_cmd->add_flag("--graph", bench_args.use_modular_graph, "Use modular RenderGraph path");
    bench_cmd->callback([&]() { exit_code = command_bench(registry, bench_args); });

    // -------------------------------------------------------------------------
    // graph
    // -------------------------------------------------------------------------
    GraphArgs graph_args;
    auto* graph_cmd = app.add_subcommand("graph", "Export the render graph as DOT");
    graph_cmd->add_option("id", graph_args.comp_id, "Composition name")->required();
    graph_cmd->add_option("--frame", graph_args.frame, "Frame to inspect")->default_val(0);
    graph_cmd->add_option("-o,--output", graph_args.output, "Output .dot path")->default_val("output/graph.dot");
    graph_cmd->callback([&]() { exit_code = command_graph(registry, graph_args); });
    // batch
    // -------------------------------------------------------------------------
    std::string batch_config;
    auto* batch_cmd = app.add_subcommand("batch", "Run multiple rendering jobs from a TOML config");
    batch_cmd->add_option("--config", batch_config, "Path to TOML config")->required();
    batch_cmd->callback([&]() { exit_code = command_batch(registry, batch_config); });

    // -------------------------------------------------------------------------
    // watch
    // -------------------------------------------------------------------------
    std::string watch_id;
    auto* watch_cmd = app.add_subcommand("watch", "Watch for changes and re-render");
    watch_cmd->add_option("id", watch_id, "Composition name")->required();
    watch_cmd->callback([&]() { exit_code = command_watch(registry, watch_id); });

    // -------------------------------------------------------------------------
    // render-all
    // -------------------------------------------------------------------------
    std::string all_output_dir;
    auto* all_cmd = app.add_subcommand("render-all", "Render frame 0 of every registered composition");
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

    // -------------------------------------------------------------------------
    // proofs
    // -------------------------------------------------------------------------
    ProofsArgs proofs_args;
    auto* proofs_cmd = app.add_subcommand("proofs", "Render proof suites for visual validation");
    proofs_cmd->add_option("suite", proofs_args.suite, "Suite name (e.g., 'fake3d', 'camera25d') or 'list' or 'all'")->required();
    proofs_cmd->add_option("-o,--output-dir", proofs_args.output_dir, "Output directory")->default_val("output/proofs");
    proofs_cmd->add_flag("--modular", proofs_args.use_modular_graph, "Use modular RenderGraph");
    proofs_cmd->add_flag("--diagnostic", proofs_args.diagnostic, "Enable diagnostic overlays");
    proofs_cmd->add_option("--ssaa", proofs_args.ssaa, "Super-sampling factor (e.g., 2.0)")->default_val(1.0f);
    proofs_cmd->callback([&]() { exit_code = command_proofs(registry, proofs_args); });

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return exit_code;
}
