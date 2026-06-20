#include "../../commands.hpp"
#include "../../cli_init.hpp"
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace chronon3d {
namespace cli {

int command_preflight(const CompositionRegistry& registry, const PreflightArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }
    if (args.start > args.end) {
        spdlog::error("--start must be <= --end");
        return 1;
    }
    if (args.sample_step < 1) {
        spdlog::error("--sample-step must be >= 1");
        return 1;
    }

    auto& preflight = RenderPreflight::instance();

    // Collect output path requirement if specified
    if (!args.output.empty()) {
        preflight.require_output_path(args.output);
    }

    // Evaluate scenes at sampled frames and register requirements
    // (This is the foundation for the SceneAssetCollector; for now we
    //  focus on the preflight infrastructure itself.)
    auto comp = registry.create(args.comp_id);

    for (Frame f = args.start; f <= args.end; f += static_cast<Frame>(args.sample_step)) {
        (void)comp.evaluate(f);
        // Future: SceneAssetCollector would traverse the scene here and
        // call preflight.add_requirements() with collected assets.
    }

    // Run validation
    auto issues = preflight.validate(cli_asset_registry());

    // Format and print terminal output
    if (!issues.empty()) {
        bool has_errors = false;
        for (const auto& i : issues) {
            if (i.severity == PreflightSeverity::Error) {
                has_errors = true;
                break;
            }
        }

        if (has_errors) {
            fmt::print(stderr, "{}", format_preflight_issues_text(issues));
        } else {
            // Only warnings / infos: print to stdout
            fmt::print("{}", format_preflight_issues_text(issues));
        }
    } else {
        fmt::print("PREFLIGHT OK — all assets and requirements validated successfully.\n");
    }

    // Write JSON output if requested
    if (!args.json_file.empty()) {
        auto js = preflight_issues_to_json(issues);
        std::ofstream out(args.json_file);
        if (out.is_open()) {
            out << js.dump(2);
            spdlog::info("Preflight report written to {}", args.json_file);
        } else {
            spdlog::error("Failed to open output json file: {}", args.json_file);
            return 1;
        }
    }

    // Determine exit code
    for (const auto& i : issues) {
        if (i.severity == PreflightSeverity::Error) return 1;
    }

    return 0;
}

} // namespace cli
} // namespace chronon3d
