#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../utils/common/cli_utils.hpp"
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <fmt/format.h>
#include <memory>
#include <string>

namespace chronon3d::cli {

namespace {

struct DevState {
    std::shared_ptr<std::string> watch_id{std::make_shared<std::string>()};
    std::shared_ptr<std::string> output_dir{std::make_shared<std::string>(chronon_artifact_path("verify", "").string())};
};

struct BatchState {
    std::shared_ptr<std::vector<std::string>> jobs{std::make_shared<std::vector<std::string>>()};
};

struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };

void register_watch(CLI::App& app, CliContext& ctx) {
    auto watch_id = std::make_shared<std::string>();
    auto* watch = app.add_subcommand("watch", "Watch for changes and re-render");
    watch->add_option("id", *watch_id, "Composition name")->required();
    watch->callback([watch_id, &ctx]() {
        ctx.exit_code = command_watch(ctx.registry, *watch_id);
    });
}

void register_render_all(CLI::App& app, CliContext& ctx) {
    auto output_dir = std::make_shared<std::string>(chronon_artifact_path("verify", "").string());
    auto* render_all = app.add_subcommand("render-all", "Render frame 0 of every registered composition");
    render_all->add_option("-o,--output-dir", *output_dir, "Output directory")->default_val(chronon_artifact_path("verify", "").string());
    render_all->callback([output_dir, &ctx]() {
        ctx.exit_code = command_verify(ctx.registry, *output_dir);
    });
}

void register_batch(CLI::App& app, CliContext& ctx) {
    auto jobs = std::make_shared<std::vector<std::string>>();
    auto* cmd = app.add_subcommand("batch", "Run multiple rendering jobs from explicit CLI job specs");
    cmd->add_option("--job", *jobs, "Job spec string: composition|frames|output|diagnostic|graph")->required();
    cmd->callback([jobs, &ctx]() {
        ctx.exit_code = command_batch(ctx.registry, *jobs);
    });
}

void register_bench_convert(CLI::App& app, CliContext& ctx) {
    auto args = std::make_shared<BenchConvertArgs>();
    auto* cmd = app.add_subcommand("benchconvert",
        "Benchmark YUV frame conversion backends (Packed, Swscale)");
    cmd->add_option("comp_id", args->comp_id, "Composition ID to render and convert")->required();
    cmd->add_option("-f,--frame",     args->frame,       "Frame index to render")->default_val(0);
    cmd->add_option("-i,--iterations",args->iterations,  "Timing iterations per backend")->default_val(10);
    cmd->add_option("--format",       args->format,
                    "Output YUV format: nv12 | yuv420p")->default_val("yuv420p");
    cmd->add_flag("--gamma,!--no-gamma", args->apply_gamma,
                  "Apply sRGB gamma when quantising linear RGB")->default_val(true);
    cmd->callback([args, &ctx]() {
        ctx.exit_code = command_bench_convert(ctx.registry, *args);
    });
}

}  // namespace (closes anonymous namespace)

// Top-level dispatcher — declared in command_registry.hpp:11 and called from
// command_registry.cpp's register_all_commands().  Mirrors the flat-mount
// convention used by register_render_commands / register_video_commands:
// each helper adds its subcommand directly to the provided `app`.  Must live
// OUTSIDE the anonymous namespace above so it has external linkage and the
// linker can resolve calls from group_dev.cpp / command_registry.cpp.
void register_cache_stats(CLI::App& app, CliContext& ctx) {
    auto* cmd = app.add_subcommand("cache-stats", "Print live cache diagnostics snapshot");
    cmd->callback([&ctx]() {
        fmt::print("{}", chronon3d::cache::format_cache_snapshot());
        ctx.exit_code = 0;
    });
}

void register_dev_commands(CLI::App& app, CliContext& ctx) {
    register_watch(app, ctx);
    register_render_all(app, ctx);
    register_batch(app, ctx);
#ifdef CHRONON3D_HAS_CLI_VIDEO
    register_bench_convert(app, ctx);
#endif
    register_cache_stats(app, ctx);
}

}  // namespace chronon3d::cli

