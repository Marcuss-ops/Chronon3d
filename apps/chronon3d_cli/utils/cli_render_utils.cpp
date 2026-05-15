#include "cli_render_utils.hpp"
#include <chronon3d/specscene/specscene.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace chronon3d {
namespace cli {

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id) {
    namespace fs = std::filesystem;

    ResolvedComposition result;

    const bool specscene_input = fs::exists(comp_id) && specscene::is_specscene_file(comp_id);

    if (specscene_input) {
        result.from_specscene = true;
        auto compiled = specscene::compile_file(comp_id, &result.diagnostics);
        if (!compiled) {
            for (const auto& d : result.diagnostics) {
                spdlog::error("{}", d);
            }
            return result; // comp is nullptr → falsy
        }

        if (!result.diagnostics.empty()) {
            for (const auto& d : result.diagnostics) {
                spdlog::warn("{}", d);
            }
        }

        result.comp = std::make_shared<Composition>(std::move(*compiled));
    } else {
        if (!registry.contains(comp_id)) {
            spdlog::error("Unknown composition or specscene file: {}", comp_id);
            return result; // comp is nullptr → falsy
        }

        auto comp_instance = registry.create(comp_id);
        result.comp = std::make_shared<Composition>(std::move(comp_instance));
    }

    return result;
}

std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings) {
    auto renderer = std::make_shared<SoftwareRenderer>();
    renderer->set_composition_registry(&registry);
    renderer->set_settings(settings);
    return renderer;
}

} // namespace cli
} // namespace chronon3d
