// ═══════════════════════════════════════════════════════════════════════════
// runtime/software_runtime_adapter.cpp
//
// Fase 4 — implementations moved from runtime/render_runtime.cpp.
// M4 init-order closure — `attach_software_backend` is the canonical
// helper for non-RenderEngine callers (i.e. the CLI) that bypass
// `RenderEngine::Impl` and would otherwise hit `RuntimeContext::backend()
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
#include "../backends/software/internal/software_processor_services.hpp"  // TICKET-118/119
#include <chronon3d/runtime/render_runtime.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>

namespace chronon3d::backends::software {

void register_builtin_processors(chronon3d::renderer::SoftwareRegistry& reg) {
    chronon3d::renderer::register_builtin_processors(reg);
}

SoftwareExecutionContext make_software_renderer_context(
    chronon3d::runtime::RuntimeContext& runtime) {
    SoftwareExecutionContext context{};
    context.runtime = &runtime;
    context.registry.graph_nodes = &runtime.graph_node_registry();
    context.registry.effects = &runtime.effect_catalog();
    context.scratch.node_cache = &runtime.node_cache();
    context.scratch.graph_cache = &runtime.graph_cache();
    context.scratch.framebuffer_pool = runtime.framebuffer_pool_shared();
    context.scratch.scheduler = &runtime.scheduler();
    context.scratch.curve_cache = &runtime.curve_cache();
    context.images.cache = &runtime.image_cache();
    context.images.surfaces = &runtime.surface_registry();
    context.text.assets = &runtime.resolver();
    context.text.prepared_cache = &runtime.gpu_text_prepared_cache();
    context.executor = &runtime.executor();
    return context;
}

SoftwareExecutionContext make_software_execution_context(
    chronon3d::SoftwareRenderer* renderer,
    chronon3d::runtime::RuntimeContext& runtime) {
    assert(renderer && "make_software_execution_context: null renderer");
    auto context = make_software_renderer_context(runtime);
    context.registry.processors = &renderer->software_registry();
    context.registry.compositions = renderer->composition_registry();

    context.images.renderer = &renderer->image_renderer();
    context.images.backend = renderer->image_backend();

    context.text.resources = renderer->text_render_resources();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    context.text.fonts = &renderer->font_engine();
#endif
    context.settings = &renderer->render_settings();
    context.backend = runtime.backend_attached() ? &runtime.backend() : nullptr;
    context.counters = renderer->counters();
    context.video_decoder = renderer->video_decoder();
    return context;
}

std::unique_ptr<chronon3d::SoftwareRenderer>
make_software_renderer(chronon3d::runtime::RuntimeContext& runtime,
                        chronon3d::Config config) {
    auto renderer = std::make_unique<chronon3d::SoftwareRenderer>(
        make_software_renderer_context(runtime), config);
    return renderer;
}

namespace {

std::unique_ptr<chronon3d::graph::RenderBackend>
make_software_backend_instance(
    chronon3d::SoftwareRenderer* renderer,
    chronon3d::runtime::RuntimeContext& runtime) {
    chronon3d::SoftwareBackendServices services{};
    services.counters         = renderer->counters();
    services.settings         = &renderer->render_settings();
    services.framebuffer_pool = runtime.framebuffer_pool_shared();
    services.asset_resolver   = &runtime.resolver();
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
    extras.curve_cache    = &runtime.curve_cache();
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

void attach_software_backend(
    chronon3d::SoftwareRenderer* renderer,
    chronon3d::runtime::RuntimeContext& runtime,
    chronon3d::graph::BackendPreference preference) {
    assert(renderer && "attach_software_backend: null renderer");
    // Idempotent — re-entry is silent so unit tests don't have to gate
    // against the transition from attach_pending to attached.
    if (runtime.backend_attached()) {
        return;
    }

    chronon3d::graph::BackendRegistry registry;
    registry.register_backend(
        chronon3d::graph::BackendType::Software,
        chronon3d::graph::BackendCapabilities{
            .graphics = true,
            .max_texture_width = 16384,
            .max_texture_height = 16384},
        [renderer, &runtime] { return make_software_backend_instance(renderer, runtime); });

#ifdef CHRONON3D_ENABLE_VULKAN
    registry.register_backend(
        chronon3d::graph::BackendType::Vulkan,
        chronon3d::graph::BackendCapabilities{
            .graphics = true, .compute = true},
        [renderer, &runtime] {
            auto backend = chronon3d::backends::vulkan::make_vulkan_backend(
                runtime.config().gpu_device_id());
            auto* vulkan = dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(
                backend.get());
            if (!vulkan) {
                throw std::runtime_error(
                    "attach_software_backend: Vulkan factory returned an unexpected backend");
            }
            return backend;
        });
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
    runtime.attach_backend(resolved.take_value());
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    renderer->attach_execution_context(
        make_software_execution_context(renderer, runtime));

}

} // namespace chronon3d::backends::software
