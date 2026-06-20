// ==============================================================================
// CLI Group: Core (basic commands)
// Contains: list, info, doctor, verify
// ==============================================================================
#include "command_registry.hpp"
#include "commands.hpp"
#include "../utils/common/cli_utils.hpp"
#include <memory>

namespace chronon3d::cli::group_core {

namespace {

struct InfoState { std::shared_ptr<std::string> id{std::make_shared<std::string>()}; };
struct VerifyState {
    std::shared_ptr<std::string> output_dir{
        std::make_shared<std::string>(chronon_artifact_path("verify", "").string())
    };
};

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
    cmd->add_option("-o,--output-dir", *state->output_dir, "Output directory")
        ->default_val(chronon_artifact_path("verify", "").string());
    cmd->callback([state, &ctx]() { ctx.exit_code = command_verify(ctx.registry, *state->output_dir); });
}

void register_daemon(CLI::App& app, CliContext& ctx) {
    auto* cmd = app.add_subcommand("daemon", "Start hot-reload daemon with warm engine state");
    auto assets_root = std::make_shared<std::string>();
    auto build_cmd = std::make_shared<std::string>("bash build-fast.sh cli");
    cmd->add_option("-a,--assets-root", *assets_root,
                    "Asset root directory (fonts, images)");
    cmd->add_option("-b,--build-cmd", *build_cmd,
                    "Build command for reload (default: bash build-fast.sh cli)");
    cmd->callback([assets_root, build_cmd, &ctx]() {
        ctx.exit_code = command_daemon(ctx.registry, *assets_root, *build_cmd);
    });
}

} // namespace

void register_commands(CLI::App& app, CliContext& ctx) {
    register_list(app, ctx);
    register_info(app, ctx);
    register_doctor(app, ctx);
    register_verify(app, ctx);
    register_daemon(app, ctx);
}

} // namespace chronon3d::cli::group_core