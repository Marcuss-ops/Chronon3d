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
#include <chronon3d/runtime/render_runtime.hpp>
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

std::unique_ptr<runtime::RenderRuntime> create_runtime_or_throw(Config config) {
    auto result = runtime::RenderRuntime::create(
        runtime::RuntimeConfig{std::move(config), std::nullopt});
    if (!result.has_value()) {
        throw std::runtime_error(
            "SoftwareRenderer: RenderRuntime::create() failed: " +
            result.error().message);
    }
    return std::move(result).value();
}

} // namespace


// ── Construction ──────────────────────────────────────────────────────────────

// Canonical path — borrows an existing RenderRuntime + overrides Config.
SoftwareRenderer::SoftwareRenderer(runtime::RenderRuntime& rt, Config config)
    : m_config(std::move(config))
    , m_owned_runtime_storage{}
    , m_runtime(&rt)
    , m_image_renderer(rt.image_cache())
    , m_software_registry(std::make_unique<renderer::SoftwareRegistry>())
    , m_text_render_resources(std::make_unique<TextRenderResources>())
{
    m_runtime->image_cache().set_asset_resolver(&m_runtime->resolver());
    m_session = backends::software::make_session(*m_runtime);
    backends::software::register_builtin_processors(*m_software_registry);
}

// @deprecated Fase C2 — creates its own runtime internally.  Production code
// MUST route through `RenderEngine(Config)` which calls the canonical ctor
// above with a pre-built `runtime::RenderRuntime` instance.
SoftwareRenderer::SoftwareRenderer(Config config)
    : m_config(std::move(config))
    , m_owned_runtime_storage(create_runtime_or_throw(m_config))
    , m_runtime(m_owned_runtime_storage.get())
    , m_image_renderer(m_runtime->image_cache())
{
    // The deprecated standalone constructor has no separate SDK asset-root
    // setter. Preserve its historical relative-path behavior by mounting
    // the current working directory once; RenderEngine users still provide
    // an explicit root through RenderEngine::set_assets_root().
    if (!m_runtime->resolver().has_mount()) {
        m_runtime->resolver().mount(std::filesystem::current_path());
    }
    m_software_registry = std::make_unique<renderer::SoftwareRegistry>();
    m_text_render_resources = std::make_unique<TextRenderResources>();
    m_runtime->image_cache().set_asset_resolver(&m_runtime->resolver());
    m_session = backends::software::make_session(*m_runtime);
    backends::software::register_builtin_processors(*m_software_registry);
}

} // namespace chronon3d
