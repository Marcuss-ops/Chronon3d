#pragma once

// ============================================================================
// include/chronon3d/backends/software/software_processor_context.hpp
//
// 06 R2 — `SoftwareProcessorContext`: slim POD passed to processor `draw()`
// methods in place of the previous `SoftwareRenderer &` first parameter.
//
// All fields are NON-OWNING raw pointers. Lifetime invariant:
//   counters, settings, registry must outlive the dispatch.
//   image_backend/image_renderer: optional, may be nullptr for non-text
//     or non-image processor paths.
//   font_engine: only set when CHRONON3D_HAS_BACKEND_TEXT.
//
// Header-only AND forward-declaration-only: do NOT pull in
// <chronon3d/backends/software/software_renderer.hpp> or any heavy
// backend header here. This header is intended to be cheapeable from
// every processor TU. The forward-decls are sufficient because all
// fields are pointers; no member-type instantiation is required.
//
// Migration helper: `make_processor_context(SoftwareRenderer*)` in
// `software_processor_context.cpp` constructs the bundle from a
// renderer instance. Single source of "pull runtime data through the
// renderer", so processors can be re-pointed to the runtime once R3
// drops dual identity.
// ============================================================================

namespace chronon3d {
struct RenderCounters;
struct RenderSettings;
struct DebugConfig;

namespace renderer {
class SoftwareRegistry;
} // namespace renderer

class ImageRenderer;
namespace image {
class ImageBackend;
} // namespace image

namespace assets {
class AssetResolver;
} // namespace assets

#ifdef CHRONON3D_HAS_BACKEND_TEXT
class FontEngine;
#endif

struct SoftwareProcessorContext {
    RenderCounters*                                  counters{nullptr};        // REQUIRED
    const RenderSettings*                            settings{nullptr};        // REQUIRED
    renderer::SoftwareRegistry*                      registry{nullptr};        // REQUIRED
    image::ImageBackend*                             image_backend{nullptr};
    ImageRenderer*                                   image_renderer{nullptr};
    const DebugConfig*                               debug_config{nullptr};    // optional, for text-bbox debug overlays
    const assets::AssetResolver*                     asset_resolver{nullptr};  // optional, for font/text resolution
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    FontEngine*                                      font_engine{nullptr};
#endif
};

/// Build a SoftwareProcessorContext from a renderer (the
/// canonical pre-R3 source). Returns by value; non-owning pointers
/// only. After R3 (when dual identity is dropped) this helper will
/// source from the runtime directly.
[[nodiscard]] SoftwareProcessorContext make_processor_context(class SoftwareRenderer* renderer);

} // namespace chronon3d
