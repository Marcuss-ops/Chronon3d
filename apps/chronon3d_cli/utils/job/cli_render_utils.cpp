#include "cli_render_utils.hpp"
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_backend_services.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

// TICKET-118/119 — internal bridge for ProcessorSourceExtras + make_processor_context
#include "internal/software_processor_services.hpp"

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
        // TICKET-118 — `owner` removed from SoftwareBackendServices.
        // Build the services bundle with aggregate initialization.
        chronon3d::SoftwareBackendServices services{
            /* counters           = */ renderer->counters(),
            /* settings           = */ &renderer->render_settings(),
            /* framebuffer_pool   = */ renderer->runtime().framebuffer_pool_shared(),
            /* asset_resolver     = */ &renderer->runtime().resolver(),
            /* text_resources     = */ renderer->text_render_resources(),
            /* images             = */ &renderer->image_renderer(),
            /* text_raster        = */ nullptr,
            /* debug_config       = */ nullptr,
        };

        auto factory_result = make_software_backend(services);
        if (!factory_result.has_value()) {
            const auto& e = factory_result.error();
            spdlog::error(
                "[cli] create_renderer: make_software_backend rejected: code={} field='{}' msg='{}'",
                static_cast<int>(e.code),
                e.field_name,
                e.message);
            assert(false && "make_software_backend service-validation failure (REQUIRED service null). See cli_render_utils.cpp::create_renderer.");
            throw std::runtime_error(
                std::string{"create_renderer: make_software_backend rejected: "}
                + e.message);
        }

        // TICKET-119 — attach orchestrator-only fields (registry /
        // image_backend / font_engine) via the internal bridge.
        backends::software::internal::ProcessorSourceExtras extras{};
        extras.registry      = &renderer->software_registry();
        extras.image_backend = renderer->image_backend();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
        extras.font_engine   = &renderer->font_engine();
#endif
        factory_result.value()->attach_processor_context(
            backends::software::internal::make_processor_context(services, extras));

        renderer->runtime().attach_backend(std::move(factory_result.value()));
    }

    return renderer;
}

} // namespace cli
} // namespace chronon3d
