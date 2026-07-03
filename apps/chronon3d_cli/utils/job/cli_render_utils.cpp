#include "cli_render_utils.hpp"
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>  // Fase A2 — attach_software_backend factory
#include <chronon3d/runtime/render_runtime.hpp>

#include <cassert>
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
        : std::make_shared<SoftwareRenderer>(Config{});
    renderer->set_composition_registry(&registry);
    renderer->set_settings(settings);

    // Inject default backends
    renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

    // FIX (M4 init-order bug): SoftwareRenderer::backend() dereferences
    // m_runtime->backend(), which throws `RenderRuntime::backend() called
    // before attach_backend()` if no SoftwareBackend has been attached to
    // the renderer's runtime.  The renderer's two ctors do NOT attach a
    // backend automatically (that step is documented in render_runtime.hpp
    // as the responsibility of RenderEngine::Impl, which the CLI doesn't
    // use — CLI goes straight through SoftwareRenderer + create_renderer).
    // Attach SoftwareBackend here AFTER all per-instance wiring
    // (image_backend / settings / registry) so the backend's service
    // pointers (counters, settings, framebuffer_pool, asset_resolver,
    // text_resources, owner) are stable and lifetime-invariant for the
    // rest of the session.  The SoftwareBackend keeps non-owning
    // references into the renderer; constructing it from inside the
    // renderer ctor would be unsafe because the shared_ptr<SoftwareRenderer>
    // further up this function could be moved before these refs were
    // read.
    assert(renderer != nullptr);
    if (!renderer->runtime().backend_attached()) {
        // Fase A2 — unified backend construction via the canonical
        // `attach_software_backend()` factory (runtime_adapter.hpp).
        // Replaces the previously-inlined services bundle +
        // make_software_backend + attach_processor_context duplicate.
        chronon3d::backends::software::attach_software_backend(renderer.get());
    }

    return renderer;
}

} // namespace cli
} // namespace chronon3d
