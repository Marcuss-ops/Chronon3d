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

