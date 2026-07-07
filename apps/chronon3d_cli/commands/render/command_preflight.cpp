#include "../../commands.hpp"
#include "../../cli_init.hpp"
#include "../../utils/common/cli_asset_preflight_utils.hpp"
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
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

    auto comp = registry.create(args.comp_id);
    auto resolver = make_cli_resolver(comp.assets_root());

    // Sequence V2: collect the AssetManifest from sampled frames
    AssetManifest manifest;
    for (Frame f = args.start; f <= args.end; f += static_cast<Frame>(args.sample_step)) {
        auto scene = comp.evaluate(f);
        manifest.merge(scene.asset_manifest());
    }

    // Run manifest-based preflight (Sequence V2)
    auto manifest_result = AssetPreflightResolver::check_manifest(manifest, resolver);

    // Also run legacy RenderPreflight for backward compatibility
    auto& preflight = RenderPreflight::instance();
    if (!args.output.empty()) {
        preflight.require_output_path(args.output);
    }
    auto legacy_issues = preflight.validate(cli_asset_registry(), resolver);

    // Combine issues
    std::vector<PreflightIssue> all_issues;
    all_issues.insert(all_issues.end(), manifest_result.issues.begin(), manifest_result.issues.end());
    all_issues.insert(all_issues.end(), legacy_issues.begin(), legacy_issues.end());

    // Format and print terminal output
    if (!all_issues.empty()) {
        bool has_errors = has_preflight_errors(all_issues);
        if (has_errors) {
            fmt::print(stderr, "{}", format_preflight_issues_text(all_issues));
        } else {
            fmt::print("{}", format_preflight_issues_text(all_issues));
        }
    } else {
        fmt::print("PREFLIGHT OK — all assets and requirements validated successfully.\n");
    }

    // Write JSON output if requested
    if (!args.json_file.empty()) {
        auto js = preflight_issues_to_json(all_issues);
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
    return has_preflight_errors(all_issues) ? 1 : 0;
}

} // namespace cli
} // namespace chronon3d
