#include "commands.hpp"
#include "cli_utils.hpp"
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <chronon3d/specscene/specscene.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fmt/format.h>

namespace chronon3d {
namespace cli {

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    auto range = parse_frames(args.frames);
    
    if (args.frame_old != -1) {
        range.start = range.end = args.frame_old;
    } else if (args.start_old != -1 || args.end_old != -1) {
        if (args.start_old != -1) range.start = args.start_old;
        if (args.end_old != -1) range.end = args.end_old;
    }

    namespace fs = std::filesystem;

    const bool specscene_input = fs::exists(args.comp_id) && specscene::is_specscene_file(args.comp_id);
    std::shared_ptr<Composition> comp_ptr;
    std::vector<std::string> specscene_diagnostics;

    if (specscene_input) {
        auto compiled = specscene::compile_file(args.comp_id, &specscene_diagnostics);
        if (!compiled) {
            for (const auto& d : specscene_diagnostics) {
                spdlog::error("{}", d);
            }
            return 1;
        }

        if (!specscene_diagnostics.empty()) {
            for (const auto& d : specscene_diagnostics) {
                spdlog::warn("{}", d);
            }
        }

        comp_ptr = std::make_shared<Composition>(std::move(*compiled));
    } else {
        if (!registry.contains(args.comp_id)) {
            spdlog::error("Unknown composition or specscene file: {}", args.comp_id);
            return 1;
        }

        auto comp_instance = registry.create(args.comp_id);
        comp_ptr = std::make_shared<Composition>(std::move(comp_instance));
    }

    auto renderer = std::make_shared<SoftwareRenderer>();
    renderer->set_composition_registry(&registry);
    
    RenderSettings settings;
    settings.diagnostic = args.diagnostic;
    settings.use_modular_graph = args.use_modular_graph;
    settings.motion_blur.enabled      = specscene_input ? false : args.motion_blur;
    settings.motion_blur.samples      = args.motion_blur_samples;
    settings.motion_blur.shutter_angle = args.shutter_angle;
    settings.ssaa_factor              = args.ssaa;
    renderer->set_settings(settings);

    if (specscene_input && args.motion_blur) {
        spdlog::warn("Motion blur is ignored for specscene inputs in this build");
    }

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
        args.comp_id, range.start, range.end, range.step,
        args.motion_blur ? fmt::format(" [MB {}smp {:.0f}°]", args.motion_blur_samples, args.shutter_angle) : "",
        args.ssaa > 1.0f ? fmt::format(" [SSAA {:.1f}x]", args.ssaa) : "");
    
    int64_t effective_end = (range.start == range.end) ? range.start + 1 : range.end;
    for (int64_t f = range.start; f < effective_end; f += range.step) {
        auto fb = renderer->render_frame(*comp_ptr, static_cast<Frame>(f));

        if (fb) {
            bool is_range = (range.start != range.end);
            std::string path = format_path(args.output, f, is_range);
            std::filesystem::path p(path);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }

            if (save_png(*fb, path)) {
                spdlog::info("Frame {} saved to {}", f, path);
            } else {
                spdlog::error("Failed to save frame {} to {}", f, path);
            }
        }
    }

    spdlog::info("Render complete.");
    return 0;
}

} // namespace cli
} // namespace chronon3d
