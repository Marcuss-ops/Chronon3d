#include "../commands.hpp"
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <filesystem>
#include <chrono>

namespace chronon3d {
namespace cli {

int command_batch(const CompositionRegistry& registry, const std::string& config_path) {
    if (!std::filesystem::exists(config_path)) {
        spdlog::error("Batch config not found: {}", config_path);
        return 1;
    }

    try {
        auto config = toml::parse_file(config_path);
        auto jobs = config["job"].as_array();
        if (!jobs) {
            spdlog::error("No 'job' array found in {}", config_path);
            return 1;
        }

        auto t0 = std::chrono::steady_clock::now();

        for (auto& node : *jobs) {
            auto tbl = node.as_table();
            if (!tbl) continue;

            RenderArgs args;
            args.comp_id = (*tbl)["composition"].value_or<std::string>("");
            args.frames = (*tbl)["frames"].value_or<std::string>("0");
            args.output = (*tbl)["output"].value_or<std::string>("render_####.png");
            args.diagnostic = (*tbl)["diagnostic"].value_or<bool>(false);
            args.use_modular_graph = (*tbl)["graph"].value_or<bool>(false);

            if (args.comp_id.empty()) {
                spdlog::warn("Skipping job with missing composition ID");
                continue;
            }

            if (command_render(registry, args) != 0) {
                spdlog::error("Batch job failed for {}", args.comp_id);
            }
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        spdlog::info("Batch complete in {}ms", ms);
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("TOML parse error: {}", e.what());
        return 1;
    }
}

} // namespace cli
} // namespace chronon3d
