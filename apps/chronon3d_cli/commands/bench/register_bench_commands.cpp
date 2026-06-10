#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <memory>

namespace chronon3d::cli {

namespace {
struct BenchState { std::shared_ptr<BenchArgs> args{std::make_shared<BenchArgs>()}; };
}

void register_bench_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<BenchState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("bench", "Benchmark a composition in-memory");
    cmd->add_option("id", args.comp_id, "Composition name")->required();
    cmd->add_option("--frames", args.frames, "Measured frames")->default_val(120);
    cmd->add_option("--warmup", args.warmup, "Warmup frames")->default_val(10);
    cmd->add_flag("--graph,!--no-graph", args.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--dirty-rects", args.dirty_rects, "Enable dirty rectangles invalidation");
    cmd->add_option("--json", args.json_file, "Path to output benchmark JSON telemetry");
    cmd->add_option("--compare", args.compare_file, "Compare against previous benchmark JSON");
    cmd->add_flag("--quiet", args.quiet, "Only print summary");
    cmd->add_flag("--include-frame-times", args.include_frame_times, "Include per-frame timings in JSON");
    cmd->add_option("--fail-if-avg-slower-pct", args.fail_if_avg_slower_pct, "Fail if avg frame time regresses by this percent")->default_val(0.0);
    cmd->add_flag("--warmup-renderer", args.warmup_renderer,
                  "Preallocate framebuffers and prime caches before rendering");
    cmd->add_option("--warmup-framebuffers", args.warmup_framebuffers,
                    "Number of framebuffers to preallocate (default 8)");
    cmd->add_flag("--warmup-dummy-frame", args.warmup_dummy_frame,
                  "Render a dummy frame 0 to prime all caches");
    cmd->callback([state, &ctx]() { ctx.exit_code = command_bench(ctx.registry, *state->args); });
}

} // namespace chronon3d::cli
