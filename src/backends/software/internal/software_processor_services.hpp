// ============================================================================
// src/backends/software/internal/software_processor_services.hpp
//
// TICKET-118/119 — Internal bridge that lets SoftwareBackend build a
// complete `chronon3d::SoftwareProcessorContext` without holding a
// `SoftwareRenderer*` back-pointer.
//
// The public `SoftwareBackendServices` struct (in include/...) carries
// counters, settings, framebuffer_pool, asset_resolver, text_resources,
// image_renderer, text_raster, debug_config.  A handful of processor-
// context fields (renderer::SoftwareRegistry*, image::ImageBackend*,
// FontEngine* on text builds) live on the orchestrating renderer and
// must be supplied separately — they cannot sit on the public struct
// without expanding the API surface.
//
// This header lives strictly under `src/backends/software/` and is
// NEVER installed or exposed in `include/chronon3d/`.  It is the
// single place that knows the public `SoftwareProcessorContext` is
// built from a `SoftwareBackendServices` + a ProcessorSourceExtras
// bundle.
//
// Cat-3 (legacy removal / arch-debt closure) compliant per AGENTS.md
// v0.1 freeze: NO new public symbols; only internal wiring.
// ============================================================================

#pragma once

#include <chronon3d/backends/software/software_processor_context.hpp>

namespace chronon3d {
    struct SoftwareBackendServices;
    namespace renderer { class SoftwareRegistry; }
    namespace image { class ImageBackend; }
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    class FontEngine;
#endif
}

namespace chronon3d::backends::software::internal {

/// Fields a `SoftwareBackend` needs beyond `SoftwareBackendServices`
/// in order to construct a complete processor-context bundle.
///
/// All fields are non-owning raw pointers; lifetime is managed by
/// the orchestrating `SoftwareRenderer` (which outlives the backend
/// by way of `~RenderRuntime()` running before `~SoftwareRenderer()`).
///
/// ABI / layout invariant: the conditional `font_engine` field is
/// `#ifdef`-gated by `CHRONON3D_HAS_BACKEND_TEXT` and MUST be visible
/// with the SAME #-macro state in every TU that constructs or reads
/// `ProcessorSourceExtras`.  In the current parent CMakeLists, the
/// macro is set uniformly on `chronon3d_backend_software` via
/// `target_compile_definitions(... PRIVATE CHRONON3D_HAS_BACKEND_TEXT)`
/// (gated on `if(TARGET chronon3d_backend_text)`), so all objects in
/// this library see the same struct layout.  This matches the
/// existing `chronon3d::SoftwareProcessorContext::font_engine` layout
/// (see `include/chronon3d/backends/software/software_processor_context.hpp`).
struct ProcessorSourceExtras {
    chronon3d::renderer::SoftwareRegistry*  registry{nullptr};
    chronon3d::image::ImageBackend*         image_backend{nullptr};
    chronon3d::ImageRenderer*               image_renderer{nullptr};
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    chronon3d::FontEngine*                  font_engine{nullptr};
#endif
};

/// Build a `chronon3d::SoftwareProcessorContext` from the public
/// `SoftwareBackendServices` bundle + the orchestrator-supplied extras.
///
/// The `SoftwareProcessorContext::renderer` field is left null:
/// the backend does not own a back-pointer to the renderer (the
/// m_owner-transient-bridge has been removed in TICKET-118).
/// All other fields are populated from `services` first, then
/// `extras` (which provides registry + image_backend + font_engine).
///
/// Returns a fully populated, by-value `SoftwareProcessorContext`.
/// Call this once after constructing the `SoftwareBackend` and
/// pass the result to `SoftwareBackend::attach_processor_context()`.
[[nodiscard]] chronon3d::SoftwareProcessorContext
make_processor_context(
    const chronon3d::SoftwareBackendServices& services,
    const ProcessorSourceExtras& extras
);

} // namespace chronon3d::backends::software::internal
