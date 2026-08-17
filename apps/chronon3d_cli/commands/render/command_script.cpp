#include "../../command_registry.hpp"
#include "../../cli_context.hpp"
#include "../../utils/semantic/semantic_script.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace chronon3d::cli {
namespace {

struct ScriptState {
    std::string input;
    std::string output;
};

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open semantic script: " + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

int run_script(const ScriptState& args) {
    try {
        const auto root = nlohmann::json::parse(read_file(args.input));
        auto decoded = semantic::decode_semantic_script(root);
        if (!decoded) {
            spdlog::error("Semantic script decode failed [{}]: {}",
                          decoded.error().path, decoded.error().message);
            return 1;
        }

        const auto plan = semantic::compile_semantic_script(decoded.value());
        const auto json = semantic::render_plan_to_json(plan);
        const std::string serialized = json.dump(2);

        if (args.output.empty()) {
            std::cout << serialized << '\n';
        } else {
            std::ofstream file(args.output);
            if (!file) {
                spdlog::error("cannot write render plan: {}", args.output);
                return 1;
            }
            file << serialized << '\n';
            spdlog::info("Wrote render plan ({} layer(s)) to {}",
                         plan.layers.size(), args.output);
        }
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Semantic script error: {}", error.what());
        return 1;
    }
}

}  // namespace

void register_script_command(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ScriptState>();
    auto* command = app.add_subcommand(
        "script",
        "Transform a semantic overlay-events script into a chronon.render-plan.v1 JSON");
    command->add_option("input", state->input, "Semantic overlay-events JSON")->required();
    command->add_option("-o,--output", state->output,
                        "Render plan JSON output (stdout when omitted)");
    command->callback([state, &ctx] { ctx.exit_code = run_script(*state); });
}

}  // namespace chronon3d::cli
