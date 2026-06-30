#pragma once

// ---------------------------------------------------------------------------
// backends/software/software_backend_services.hpp
//
// TICKET-011b (advance declaration in 011a) — declare
// `chronon3d::SoftwareBackendServices`, the typed bundle of services
// injected into `chronon3d::SoftwareBackend` at construction.
// Replaces the legacy 3-arg ctor signature:
//
//   SoftwareBackend(RenderCounters&,
//                    const RenderSettings&,
//                    std::shared_ptr<cache::FramebufferPool>);
//
// with a single services-object ctor:
//
//   SoftwareBackend(SoftwareBackendServices services);
//
// All fields are non-owning raw pointers.  Lifetime is the caller's
// responsibility: SoftwareRenderer's RenderRuntime lifetime (engine),
// or a session's lifetime in tests.  Pointers default to nullptr; ctor
// will assert non-null for the critical fields (counters + framebuffer_pool).
//
// This header is INTENTIONALLY HEADER-ONLY in TICKET-011a
// (advance declaration ahead of the ctor change in 011b).  No
// consumer changes; no impact on the current build.  Sub-commit 011b
// will:
//   - Update SoftwareBackend ctor to take SoftwareBackendServices.
//   - Update SoftwareRenderer members to populate services before ctor.
//   - Validate build with `cmake --build build/chronon/linux-ci`.
// ---------------------------------------------------------------------------

#include <memory>

namespace chronon3d {
    struct RenderSettings;
    struct RenderCounters;
    class DebugConfig;
    class ImageRenderer;
}

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d {
    struct TextRenderResources;
}

namespace chronon3d::assets {
    class AssetResolver;
}

namespace chronon3d::backends::text {
    // TICKET-011d — defined under include/chronon3d/backends/text/.
    class TextRasterService;
}

namespace chronon3d {

/// SoftwareBackendServices — flat pointer bundle that mirrors what
/// `SoftwareBackend` needs at construction time.  Replaces the
/// legacy multi-arg SoftwareBackend ctor + m_owner back-pointer with
/// a single services-object ctor (TICKET-011b / Fase 4).
///
/// All pointers are non-owning and must outlive the backend.  Lifetime
/// invariant: `SoftwareBackendServices ≤ RenderRuntime lifetime`.
///
/// Pointer expectations at construction:
///   - counters          : REQUIRED.  Atomic counters, mutated in-place
///                         by apply_blur / composite / draw_node.
///   - settings          : REQUIRED.  Render-policy carrier (force_scalar,
///                         etc.).
///   - framebuffer_pool  : REQUIRED.  Acquire path for OwnedFBs.
///   - asset_resolver    : REQUIRED for text_run.  Font path resolution.
///   - text_resources    : REQUIRED for text_run.  Pre-loaded font caches.
///   - images            : optional.  Nullptr skips image node draw.
///   - text_raster       : optional in 011a; becomes REQUIRED once
///                         TICKET-011d lands.
///   - debug_config      : optional.  Nullptr suppresses per-instance
///                         debug overlays.
struct SoftwareBackendServices {
    RenderCounters*                                  counters{nullptr};
    const RenderSettings*                            settings{nullptr};
    std::shared_ptr<cache::FramebufferPool>          framebuffer_pool;
    const assets::AssetResolver*                     asset_resolver{nullptr};
    TextRenderResources*                             text_resources{nullptr};
    ImageRenderer*                                   images{nullptr};
    chronon3d::backends::text::TextRasterService*   text_raster{nullptr};
    const DebugConfig*                               debug_config{nullptr};
};

} // namespace chronon3d
