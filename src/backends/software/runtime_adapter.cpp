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
#include <chronon3d/render_graph/backend_registry.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif

#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_backend_services.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>  // TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE: std::unique_ptr<TextRenderResources> needs the complete type (sizeof-incomplete at the default-deleter instantiation site).
#include "internal/software_processor_services.hpp"  // TICKET-118/119 (PUBLIC via parent CMakeLists)
#include <chronon3d/runtime/render_runtime.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>

namespace chronon3d::backends::software {

chronon3d::SoftwareRenderSession make_session(
    chronon3d::runtime::RenderRuntime& runtime
) {
    chronon3d::SoftwareRenderSession session;
    // P1-15 — use RenderRuntime's typed direct accessors (canonical
    // surface; the `RenderServices` pointer bundle + `services()`
    // accessor have been REMOVED wholesale from RenderRuntime).
    // NOTE: the graph-context `SessionServices` type populated here
    // is distinct from the (now-deleted) runtime `RenderServices`
    // bundle — `SessionServices` is the per-`RenderSession` bridge,
    // not the per-`RenderRuntime` pointer collection.
    session.common.services = chronon3d::runtime::SessionServices{
        .executor            = &runtime.executor(),
        .node_cache          = &runtime.node_cache(),
        .framebuffer_pool    = &runtime.framebuffer_pool(),
        .graph_cache         = &runtime.graph_cache(),
        .asset_registry      = &runtime.assets(),
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

namespace {

std::unique_ptr<chronon3d::graph::RenderBackend>
make_software_backend_instance(chronon3d::SoftwareRenderer* renderer) {
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
        throw std::runtime_error(std::string{"attach_software_backend: "} + e.message);
    }

    auto backend = std::move(factory_result.value());
    internal::ProcessorSourceExtras extras{};
    extras.registry       = &renderer->software_registry();
    extras.image_backend  = renderer->image_backend();
    extras.image_renderer = &renderer->image_renderer();
    extras.curve_cache    = &renderer->runtime().curve_cache();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    extras.font_engine    = &renderer->font_engine();
#endif
    auto processor_context = internal::make_processor_context(services, extras);
    processor_context.image_renderer = &renderer->image_renderer();
    processor_context.image_backend = renderer->image_backend();
    backend->attach_processor_context(std::move(processor_context));
    backend->attach_image_services(&renderer->image_renderer(),
                                   renderer->image_backend());
    return backend;
}

} // namespace

void attach_software_backend(chronon3d::SoftwareRenderer* renderer) {
    attach_software_backend(renderer, chronon3d::graph::BackendPreference::Auto);
}

void attach_software_backend(
    chronon3d::SoftwareRenderer* renderer,
    chronon3d::graph::BackendPreference preference) {
    assert(renderer && "attach_software_backend: null renderer");
    // Idempotent — re-entry is silent so unit tests don't have to gate
    // against the transition from attach_pending to attached.
    if (renderer->runtime().backend_attached()) {
        return;
    }

    chronon3d::graph::BackendRegistry registry;
    registry.register_backend(
        chronon3d::graph::BackendType::Software,
        chronon3d::graph::BackendCapabilities{
            .graphics = true,
            .max_texture_width = 16384,
            .max_texture_height = 16384},
        [renderer] { return make_software_backend_instance(renderer); });

#ifdef CHRONON3D_ENABLE_VULKAN
    // Vulkan is strict-opt-in until RenderSurface execution replaces the
    // current CPU Framebuffer node contract. Auto remains a safe CPU fallback
    // during this migration and never silently runs a partial GPU path.
    if (preference == chronon3d::graph::BackendPreference::GPU) {
        registry.register_backend(
            chronon3d::graph::BackendType::Vulkan,
            chronon3d::graph::BackendCapabilities{
                .graphics = true, .compute = true},
            [] { return chronon3d::backends::vulkan::make_vulkan_backend(); });
    }
#endif

    chronon3d::graph::BackendResolver resolver(registry);
    auto resolved = resolver.resolve(preference);
    if (!resolved.has_value()) {
        const auto& error = resolved.error();
        spdlog::error("[backend] selection failed: preference={} code={} message={}",
                      chronon3d::graph::backend_preference_name(preference),
                      static_cast<int>(error.code), error.message);
        throw std::runtime_error("backend selection failed: " + error.message);
    }

    // Fase C2 — attach_backend() is [[deprecated]] for public consumers.
    // This bridge is the canonical internal orchestration path; suppress
    // the deprecation warning so CI stays clean.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    renderer->runtime().attach_backend(resolved.take_value());
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}

} // namespace chronon3d::backends::software
