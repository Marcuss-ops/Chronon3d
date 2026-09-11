// ===========================================================================
// src/backends/software/software_renderer_factory.cpp
//
// M1.5#12 1/4 — SoftwareRenderer construction ctors (the "factory" half of
// the 06-stabilization split pattern, mirroring `software_backend_factory.cpp`).
//
// Single source of truth for renderer construction.  The 2 ctors live here
// (no move ops, no dtor, no setters).  Move ops / settings / orchestration
// live in `software_renderer.cpp`.  This split keeps each TU under one
// responsibility boundary in the spirit of the M1.5#12 decomposition and
// keeps the orphan helpers (anonymous-namespace `to_local_clip`,
// `clipped_area`, `RenderIOFenceGuard`) confined to the TUs that actually
// use them (dispatch.cpp / text.cpp respectively).
//
// Anti-duplication invariant (strict): ONLY the 2 ctors live here.  The
// class header (`include/chronon3d/backends/software/software_renderer.hpp`)
// is the SOLE source of declarations; this TU is the sole source of ctor
// bodies.
//
// Construction rationale carried ver-bit from the pre-split incarnation:
//   * Per-renderer FontEngine is constructed from `m_runtime->resolver()`
//     (PR 8.1 ownership transfer).
//   * Per-runtime ImageCache wiring (Fase B B1 — replaces process-wide
//     singleton).
//   * Built-in processors registered at startup (shape_processor lineage).
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
// IMPORTANT: include `runtime_adapter.hpp` (NOT `builtin_processors.hpp`).
// The canonical `backends::software::register_builtin_processors(...)`
// wrapper lives in `runtime_adapter.hpp` (namespace
// `chronon3d::backends::software`); `builtin_processors.hpp` declares the
// inner-namespace `chronon3d::renderer::register_builtin_processors(...)`
// which would resolve to the wrong function if pulled via this include.
#include <chronon3d/backends/software/runtime_adapter.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#endif
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace chronon3d {

namespace {
ImageCache& require_image_cache(const SoftwareExecutionContext& context) {
    if (!context.images.cache) {
        throw std::invalid_argument(
            "SoftwareRenderer: execution context has no image cache");
    }
    return *context.images.cache;
}
} // namespace

// ── Construction ──────────────────────────────────────────────────────────────

// Canonical path — receives an explicit dependency view; runtime lifetime is
// owned by the caller and is never stored on SoftwareRenderer.
SoftwareRenderer::SoftwareRenderer(SoftwareExecutionContext context, Config config)
    : m_config(std::move(config))
    , m_execution_context(std::move(context))
    , m_image_renderer(require_image_cache(m_execution_context))
    , m_software_registry(std::make_unique<renderer::SoftwareRegistry>())
    , m_text_render_resources(std::make_unique<TextRenderResources>())
    , m_session()
{
    m_execution_context.registry.processors = m_software_registry.get();
    m_execution_context.images.renderer = &m_image_renderer;
    m_execution_context.text.resources = m_text_render_resources.get();
    m_execution_context.settings = &m_settings;
    m_execution_context.counters = &m_counters;
    backends::software::register_builtin_processors(*m_software_registry);
}

} // namespace chronon3d
