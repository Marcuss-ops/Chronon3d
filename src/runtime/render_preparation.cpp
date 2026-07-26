#include <chronon3d/runtime/render_preparation.hpp>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <sstream>

namespace chronon3d::runtime {

std::string RenderPreparationResult::diagnostic() const {
    std::ostringstream out;
    for (const auto& issue : preflight.issues) {
        out << issue.message << '\n';
    }
    if (preparation_error) {
        out << preparation_error->message;
        if (!preparation_error->path.empty()) {
            out << " (" << preparation_error->path << ')';
        }
    }
    return out.str();
}

RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const Composition& composition,
    const RenderPreparationOptions& options) {
    RenderPreparationResult result;
    if (renderer == nullptr) {
        result.preparation_error = PreparationError{
            .code = PreparationError::Code::InternalError,
            .message = "Render preparation requires a renderer",
            .phase = "setup",
        };
        return result;
    }

    // Composition evaluation is intentionally random-access and collects
    // inactive sequence assets as part of the canonical scene manifest.
    const Scene scene = composition.evaluate(options.reference_frame);
    result.preflight = AssetPreflightResolver::check(
        scene, renderer->runtime().resolver(), options.preflight_mode,
        options.reference_frame);
    if (!result.preflight.ok()) {
        return result;
    }

    auto prepared = ResourcePreparation::prepare(
        scene.asset_manifest(), renderer->runtime().resolver(), options.resources);
    if (!prepared.has_value()) {
        result.preparation_error = std::move(prepared.error());
        return result;
    }
    result.prepared_assets = std::move(prepared.value());

    // Complete the two phases that have canonical runtime services. The
    // ResourcePreparation pass above remains the deterministic manifest
    // barrier; these calls perform the actual cache population before the
    // first frame and therefore make invalid payloads fail early.
    if (options.resources.prepare_fonts &&
        !scene.asset_manifest().filter(assets::AssetKind::Font).empty()) {
        const auto fonts = renderer->preflight_fonts(
            scene, renderer->runtime().resolver());
        if (fonts.preflight_missing != 0) {
            result.preparation_error = PreparationError{
                .code = PreparationError::Code::CorruptedAsset,
                .message = "one or more fonts could not be loaded",
                .phase = "font",
            };
            return result;
        }
    }

    if (options.resources.prepare_images) {
        for (const auto& ref : scene.asset_manifest().filter(assets::AssetKind::Image)) {
            const auto image = renderer->runtime().image_cache().get_or_load(ref.path);
            if (!image || !image->valid()) {
                result.preparation_error = PreparationError{
                    .code = PreparationError::Code::CorruptedAsset,
                    .message = "image could not be decoded: " + ref.path,
                    .path = ref.path,
                    .owner = ref.owner,
                    .phase = "image",
                };
                return result;
            }
            if (auto it = result.prepared_assets->images.find(ref.owner);
                it != result.prepared_assets->images.end()) {
                it->second.width = image->width;
                it->second.height = image->height;
            }
        }
    }

    if (options.warmup_renderer) {
        result.warmup = warmup_renderer(
            *renderer, composition, options.warmup);
        result.warmup_performed = true;
    }
    return result;
}

} // namespace chronon3d::runtime
