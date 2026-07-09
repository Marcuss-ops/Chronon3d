#include "../../commands.hpp"
#include "../../cli_init.hpp"
#include "../../utils/common/cli_asset_preflight_utils.hpp"
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

int command_still(const CompositionRegistry& registry, const StillArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }

    auto comp = registry.create(args.comp_id);

    // Sequence V2: FrameOnly preflight — only check assets from layers
    // active at the requested frame.  This avoids false negatives from
    // assets used at distant frames (e.g. a video referenced at frame 900
    // when rendering frame 10).
    if (!args.skip_preflight) {
        auto resolver = make_cli_resolver(comp.assets_root());

        // Evaluate the scene at the target frame to get the manifest
        FrameContext still_ctx{
            .frame = args.frame,
            .local_frame = args.frame,
            .frame_time = 0.0f,
            .duration = comp.duration(),
            .frame_rate = comp.frame_rate(),
            .width = comp.width(),
            .height = comp.height(),
            .assets_root = comp.assets_root(),
            .resource = std::pmr::get_default_resource(),
            .font_engine = nullptr,
        };
        auto scene = comp.evaluate(still_ctx);

        auto result = AssetPreflightResolver::check(
            scene, resolver, PreflightMode::FrameOnly, args.frame);

        if (!result.ok()) {
            fmt::print(stderr, "{}", format_preflight_issues_text(result.issues));
            return 1;
        }

        if (!result.empty()) {
            // Warnings only (no errors)
            fmt::print("{}", format_preflight_issues_text(result.issues));
        } else {
            spdlog::info("Preflight OK — all assets for frame {} validated.", args.frame);
        }
    }

    // Delegate to the existing render pipeline for a single frame.
    // Build a RenderArgs with the requested frame and pipeline settings.
    RenderArgs render_args;
    render_args.comp_id = args.comp_id;
    render_args.frames = std::to_string(args.frame.integral());
    render_args.output = args.output;
    render_args.log_level = args.log_level;
    render_args.pipeline = args.pipeline;

    // Default output path if not specified
    if (render_args.output.empty()) {
        std::filesystem::path comp_path(render_args.comp_id);
        std::string comp_basename = comp_path.stem().string();
        if (comp_basename.empty()) {
            comp_basename = "still";
        }
        render_args.output = "output/" + comp_basename
                             + "_f" + std::to_string(args.frame.integral()) + ".png";
        spdlog::info("No output path specified, defaulting to {}", render_args.output);
    }

    return command_render(registry, render_args);
}

} // namespace cli
} // namespace chronon3d
