#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <memory>

namespace chronon3d::cli {

namespace {

struct InfoState { std::shared_ptr<std::string> id{std::make_shared<std::string>()}; };
struct VerifyState { std::shared_ptr<std::string> output_dir{std::make_shared<std::string>("output/verify")}; };

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

} // namespace

void register_basic_commands(CLI::App& app, CliContext& ctx) {
    register_list(app, ctx);
    register_info(app, ctx);
    register_doctor(app, ctx);
    register_verify(app, ctx);
}

} // namespace chronon3d::cli
