#include "cli_render_utils.hpp"
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id) {
    ResolvedComposition result;

    if (!registry.contains(comp_id)) {
        spdlog::error("Unknown composition: {}", comp_id);
        return result;
    }

    auto comp_instance = registry.create(comp_id);
    result.comp = std::make_shared<Composition>(std::move(comp_instance));
    return result;
}

std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings,
    std::optional<Config> config) {
    auto renderer = config.has_value()
        ? std::make_shared<SoftwareRenderer>(std::move(*config))
        : std::make_shared<SoftwareRenderer>();
    renderer->set_composition_registry(&registry);
    renderer->set_settings(settings);

    // Inject default backends
    renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

    return renderer;
}

} // namespace cli
} // namespace chronon3d
