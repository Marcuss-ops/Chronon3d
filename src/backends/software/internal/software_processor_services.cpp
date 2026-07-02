// ============================================================================
// src/backends/software/internal/software_processor_services.cpp
//
// TICKET-118/119 — Implementation of internal::make_processor_context.
// Pure derivation; no logging, no IO.  Returns a temporary
// SoftwareProcessorContext by value; the caller is expected to
// cache it on the SoftwareBackend via attach_processor_context().
// ============================================================================

#include "software_processor_services.hpp"
#include <chronon3d/backends/software/software_backend_services.hpp>
#include <chronon3d/backends/software/software_registry.hpp>

namespace chronon3d::backends::software::internal {

chronon3d::SoftwareProcessorContext
make_processor_context(
    const chronon3d::SoftwareBackendServices& services,
    const ProcessorSourceExtras& extras
) {
    chronon3d::SoftwareProcessorContext ctx{};
    ctx.renderer        = nullptr;          // TICKET-118: owner-pointer removed.
    ctx.counters        = services.counters;
    ctx.settings        = services.settings;
    ctx.registry        = extras.registry;
    ctx.image_backend   = extras.image_backend;
    ctx.image_renderer  = services.images;
    ctx.asset_resolver  = services.asset_resolver;
    ctx.debug_config    = services.debug_config;
    ctx.text_resources  = services.text_resources;
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    ctx.font_engine      = extras.font_engine;
#endif
    return ctx;
}

} // namespace chronon3d::backends::software::internal
