#include "../command_registry.hpp"
#include "../commands.hpp"
#include <memory>

namespace chronon3d::cli {

namespace {

struct DevState {
    std::shared_ptr<std::string> watch_id{std::make_shared<std::string>()};
    std::shared_ptr<std::string> output_dir{std::make_shared<std::string>("output/verify")};
};

struct BatchState {
    std::shared_ptr<std::vector<std::string>> jobs{std::make_shared<std::vector<std::string>>()};
};

void register_dev(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<DevState>();
    auto* dev = app.add_subcommand("dev", "Development and verification tools");

    auto* watch = dev->add_subcommand("watch", "Watch for changes and re-render");
    watch->add_option("id", *state->watch_id, "Composition name")->required();
    watch->callback([state, &ctx]() {
        ctx.exit_code = command_watch(ctx.registry, *state->watch_id);
    });

    auto* render_all = dev->add_subcommand("render-all", "Render frame 0 of every registered composition");
    render_all->add_option("-o,--output-dir", *state->output_dir, "Output directory")->default_val("output/verify");
    render_all->callback([state, &ctx]() {
        for (const auto& id : ctx.registry.available()) {
            RenderArgs args;
            args.comp_id = id;
            args.frames = "0";
            args.output = *state->output_dir + "/" + id + ".png";
            if (command_render(ctx.registry, args) != 0) {
                ctx.exit_code = 1;
            }
        }
    });
}

void register_batch(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<BatchState>();
    auto* cmd = app.add_subcommand("batch", "Run multiple rendering jobs from explicit CLI job specs");
    cmd->add_option("--job", *state->jobs, "Job spec string: composition|frames|output|diagnostic|graph")->required();
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_batch(ctx.registry, *state->jobs);
    });
}

} // namespace

void register_dev_commands(CLI::App& app, CliContext& ctx) {
    register_dev(app, ctx);
    register_batch(app, ctx);
}

} // namespace chronon3d::cli
