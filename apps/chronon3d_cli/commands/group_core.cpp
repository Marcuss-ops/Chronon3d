#include "command_registry.hpp"
#include "commands.hpp"
#include "../utils/common/cli_utils.hpp"

#include <memory>

namespace chronon3d::cli::group_core {

namespace {

struct InfoState {
    std::shared_ptr<std::string> id{std::make_shared<std::string>()};
};

struct VerifyState {
    std::shared_ptr<std::string> output_dir{
        std::make_shared<std::string>(
            chronon_artifact_path("verify", "").string())
    };
};

void register_list(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "list", "List registered compositions and metadata");
    command->callback([&ctx]() {
        ctx.exit_code = command_list(ctx.registry);
    });
}

void register_info(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<InfoState>();
    auto* command = app.add_subcommand(
        "info", "Inspect a composition descriptor");
    command->add_option("id", *state->id, "Composition name")->required();
    command->callback([state, &ctx]() {
        ctx.exit_code = command_info(ctx.registry, *state->id);
    });
}

void register_doctor(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "doctor", "Check whether the local Chronon3d environment is ready");
    command->callback([&ctx]() {
        ctx.exit_code = command_doctor(ctx.registry);
    });
}

void register_verify(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<VerifyState>();
    auto* command = app.add_subcommand(
        "verify", "Run a quick render and video smoke test");
    command->add_option("-o,--output-dir", *state->output_dir,
                        "Output directory")
        ->default_val(chronon_artifact_path("verify", "").string());
    command->callback([state, &ctx]() {
        ctx.exit_code = command_verify(ctx.registry, *state->output_dir);
    });
}

void register_daemon(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "daemon", "Start a warm render shell with persistent caches");
    auto assets_root = std::make_shared<std::string>();
    auto build_command = std::make_shared<std::string>("bash build-fast.sh cli");
    command->add_option("-a,--assets-root", *assets_root,
                        "Asset root directory (fonts, images)");
    command->add_option("-b,--build-cmd", *build_command,
                        "Build command used by the manual reload action");
    command->callback([assets_root, build_command, &ctx]() {
        ctx.exit_code = command_daemon(
            ctx.registry, *assets_root, *build_command);
    });
}

} // namespace

void register_commands(CLI::App& app, CliContext& ctx) {
    register_list(app, ctx);
    register_info(app, ctx);
    register_doctor(app, ctx);
    register_verify(app, ctx);
    register_daemon(app, ctx);
    register_create_commands(app, ctx);
}

} // namespace chronon3d::cli::group_core
