#include "../command_registry.hpp"
#include "../commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {
struct GraphState { std::shared_ptr<GraphArgs> args{std::make_shared<GraphArgs>()}; };
}

void register_inspect_commands(CLI::App& app, CliContext& ctx) {
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

} // namespace chronon3d::cli
