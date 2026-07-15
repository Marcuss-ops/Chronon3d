#include "../../commands.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace chronon3d::cli {

int command_still(const CompositionRegistry& registry, const StillArgs& args) {
    std::string output = args.output;
    if (output.empty()) {
        std::string name = std::filesystem::path(args.comp_id).stem().string();
        if (name.empty()) name = "still";
        output = "output/" + name + "_f" +
                 std::to_string(args.frame.integral()) + ".png";
    }

    fmt::print(stderr,
               "Deprecated: use chronon render {} --frame {} -o {}\n",
               args.comp_id, args.frame.integral(), output);

    if (!args.skip_preflight) {
        spdlog::debug(
            "The deprecated still alias no longer owns a separate preflight; "
            "use 'chronon preflight' when an explicit asset check is required.");
    }

    RenderArgs render_args;
    render_args.comp_id = args.comp_id;
    render_args.frames = std::to_string(args.frame.integral());
    render_args.output = std::move(output);
    render_args.log_level = args.log_level;
    render_args.pipeline = args.pipeline;

    return command_render(registry, render_args);
}

} // namespace chronon3d::cli
