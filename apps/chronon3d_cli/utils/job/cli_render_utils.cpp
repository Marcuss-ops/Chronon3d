#include "cli_render_utils.hpp"
// specscene/model/specscene.hpp + specscene/compiler/specscene_compiler.hpp were removed
// during the Camera V1 cleanup pass. The specscene branch of resolve_composition()
// is gated with #if 0 below and `specscene_input` is hardcoded to false.
// To restore: re-add the includes, set `specscene_input` to the specscene file check,
// and uncomment the specscene branch.
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace chronon3d {
namespace cli {

namespace {


} // namespace

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id) {
    namespace fs = std::filesystem;

    ResolvedComposition result;

    // specscene_input hardcoded to false — specscene model+compiler headers were removed.
    // Restore the specscene::is_specscene_file(comp_id) check + the specscene branch below
    // if the specscene module is reintroduced.
    const bool specscene_input = false;

    if (specscene_input) {
#if 0 // TODO: SPECSCENE_API_REMOVED — specscene pipeline deleted, branch disabled.
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
#endif
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

    // Inject default backends
    renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

    return renderer;
}

} // namespace cli
} // namespace chronon3d
