#include "../command_registry.hpp"
#include "../commands.hpp"
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
    cmd->add_flag("--graph", args.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_option("--json", args.json_file, "Path to output benchmark JSON telemetry");
    cmd->callback([state, &ctx]() { ctx.exit_code = command_bench(ctx.registry, *state->args); });
}

} // namespace chronon3d::cli
