// ═══════════════════════════════════════════════════════════════════════════
// backends/software/runtime_adapter.cpp
//
// Fase 4 — implementations moved from runtime/render_runtime.cpp.
// M4 init-order closure — `attach_software_backend` is the canonical
// helper for non-RenderEngine callers (i.e. the CLI) that bypass
// `RenderEngine::Impl` and would otherwise hit `RenderRuntime::backend()
// called before attach_backend()`.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/backends/software/runtime_adapter.hpp>

#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_backend_services.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include "internal/software_processor_services.hpp"  // TICKET-118/119 (PUBLIC via parent CMakeLists)
#include <chronon3d/runtime/render_runtime.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace chronon3d::backends::software {

chronon3d::SoftwareRenderSession make_session(
    chronon3d::runtime::RenderRuntime& runtime
) {
    chronon3d::SoftwareRenderSession session;
    session.common.services = chronon3d::runtime::SessionServices{
        .executor            = runtime.services().executor,
        .node_cache          = runtime.services().node_cache,
        .framebuffer_pool    = runtime.services().framebuffer_pool,
        .graph_cache         = runtime.services().graph_cache,
        .asset_registry      = runtime.services().asset_registry,
    };
    return session;
}

const chronon3d::runtime::SessionServices& session_services(
    const chronon3d::SoftwareRenderSession& session
) {
    return session.common.services;
}

void register_builtin_processors(chronon3d::renderer::SoftwareRegistry& reg) {
    chronon3d::renderer::register_builtin_processors(reg);
}

void attach_software_backend(chronon3d::SoftwareRenderer* renderer) {
    assert(renderer && "attach_software_backend: null renderer");
    // Idempotent — re-entry is silent so unit tests don't have to gate
    // against the transition from attach_pending to attached.
    if (renderer->runtime().backend_attached()) {
        return;
    }

    // TICKET-118/119 — `SoftwareBackendServices::owner` REMOVED.  The
    // public services bundle no longer carries the SoftwareRenderer*
    // back-pointer; orchestrator-only fields (registry / image_backend /
    // font_engine) are attached post-construction via
    // `SoftwareBackend::attach_processor_context()`.  Lifetime invariant
    // (preserved): renderer outlives the backend because `renderer->runtime()`
    // owns the backend and `~RenderRuntime()` runs BEFORE `~SoftwareRenderer()`.
    chronon3d::SoftwareBackendServices services{};
    services.counters         = renderer->counters();
    services.settings         = &renderer->render_settings();
    services.framebuffer_pool = renderer->runtime().framebuffer_pool_shared();
    services.asset_resolver   = &renderer->runtime().resolver();
    services.text_resources   = renderer->text_render_resources();
    services.images           = &renderer->image_renderer();

    auto factory_result = make_software_backend(services);
    if (!factory_result.has_value()) {
        const auto& e = factory_result.error();
        spdlog::error(
            "[backends::software] attach_software_backend rejected: code={} field='{}' msg='{}'",
            static_cast<int>(e.code),
            e.field_name,
            e.message);
        throw std::runtime_error(
            std::string{"attach_software_backend: "} + e.message);
    }

    // TICKET-119 — wire the orchestrator-only fields through the internal
    // bridge (`src/backends/software/internal/software_processor_services.hpp`).
    // The resulting context carries nullptr `renderer` (m_owner eliminated)
    // and a populated registry + image_backend [+ font_engine] triplet.
    auto backend = std::move(factory_result.value());
    internal::ProcessorSourceExtras extras{};
    extras.registry      = renderer->software_registry();
    extras.image_backend = renderer->image_backend();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    extras.font_engine   = renderer->font_engine();
#endif
    backend->attach_processor_context(
        internal::make_processor_context(services, extras));

    renderer->runtime().attach_backend(std::move(backend));
}

} // namespace chronon3d::backends::software
