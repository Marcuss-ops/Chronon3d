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

namespace chronon3d::backends::text {
    // TICKET-011d — defined under include/chronon3d/backends/text/.
    class TextRasterService;
}

namespace chronon3d {

/// SoftwareBackendServices — flat pointer bundle that mirrors what
/// `SoftwareBackend` needs at construction time.  Replaces the
/// 3-arg SoftwareBackend ctor with a single services-object ctor
/// (TICKET-011b).
///
/// All pointers are non-owning and must outlive the backend.  Lifetime
/// invariant: `SoftwareBackendServices ≤ RenderRuntime lifetime`.
///
/// Pointer expectations at construction:
///   - counters          : REQUIRED.  Atomic counters, mutated in-place
///                         by apply_blur / composite / draw_node.
///   - settings          : REQUIRED.  Render-policy carrier (force_scalar,
///                         etc.); preserved here for the few legacy
///                         overrides that still consult m_settings.
///   - framebuffer_pool  : REQUIRED.  Acquire path for OwnedFBs.
///   - images            : optional.  Nullptr skips image node draw.
///   - text_raster       : optional in 011a; becomes REQUIRED once
///                         TICKET-011d lands (TextRasterService
///                         replaces static anon cache).
///   - debug_config      : optional.  Nullptr suppresses per-instance
///                         debug overlays in text/blur pipelines.
struct SoftwareBackendServices {
    RenderCounters*                                  counters{nullptr};
    const RenderSettings*                            settings{nullptr};
    cache::FramebufferPool*                          framebuffer_pool{nullptr};
    ImageRenderer*                                   images{nullptr};
    chronon3d::backends::text::TextRasterService*   text_raster{nullptr};
    const DebugConfig*                               debug_config{nullptr};
};

} // namespace chronon3d
