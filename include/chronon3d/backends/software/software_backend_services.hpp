#pragma once

// ---------------------------------------------------------------------------
// backends/software/software_backend_services.hpp
//
// TICKET-011b + Fase 1 services-validation follow-up — declare
// `chronon3d::SoftwareBackendServices`, the typed bundle of services
// injected into `chronon3d::SoftwareBackend` at construction, AND
// `chronon3d::SoftwareBackendServicesError` for the structured-release-
// path failure mode of `make_software_backend`.
//
// Replaces the legacy 4-arg ctor signature:
//
//   SoftwareBackend(SoftwareRenderer*, RenderCounters&,
//                    const RenderSettings&,
//                    std::shared_ptr<cache::FramebufferPool>);
//
// with a single services-object ctor:
//
//   SoftwareBackend(SoftwareBackendServices services);
//
// driven by the validation-gate factory:
//
//   Result<std::unique_ptr<SoftwareBackend>, SoftwareBackendServicesError>
//   make_software_backend(SoftwareBackendServices services);
//
// Pointer expectations at construction (validated by `make_software_backend`):
//   - owner              : REQUIRED (SoftwareRenderer*, non-owning).
//                          Orchestrator for the SoftwareProcessorContext
//                          bundle.  TICKET-046 follow-up will source from
//                          RenderRuntime directly; owner becomes null-tolerable.
//   - counters           : REQUIRED (RenderCounters*, non-owning).  Atomic
//                          counters, mutated in-place by apply_blur /
//                          composite / draw_node.
//   - settings           : REQUIRED (const RenderSettings*, non-owning).
//                          Render-policy carrier (force_scalar, etc.).
//   - framebuffer_pool   : REQUIRED (std::shared_ptr<cache::FramebufferPool>).
//                          Shared ownership so the same pool can be used by
//                          multiple backends; the factory stores the shared
//                          on the backend so RenderBackend::framebuffer_pool()
//                          can return by-value.
//   - asset_resolver     : REQUIRED (const assets::AssetResolver*, non-owning).
//                          Typed sibling of AssetRegistry; draw_text_run
//                          dereferences this to resolve font paths.  Past
//                          contracts allowed null + font_engine fallback;
//                          the post-fix contract is REQUIRED.
//   - text_resources     : REQUIRED (TextRenderResources*, non-owning).
//                          Pre-loaded font caches (BLFontFace + FreeType
//                          face + glyph outline cache).
//   - images             : optional (ImageRenderer*, nullptr skips image
//                          node draw).
//   - text_raster        : optional (TextRasterService*); becomes REQUIRED
//                          once TICKET-011d lands.
//   - debug_config       : optional (const DebugConfig*, nullptr suppresses
//                          per-instance debug overlays in text/blur pipelines).
//
// All five REQUIRED services + their expected-lifetime invariants are
// validated by `make_software_backend`: debug builds `assert(...)`, release
// returns `Result::err(SoftwareBackendServicesError)` carrying the field
// name + Code.
// ---------------------------------------------------------------------------

#include <chronon3d/core/types/result.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d {
    struct RenderSettings;
    struct RenderCounters;
    class DebugConfig;
    class ImageRenderer;
    class SoftwareRenderer;
    namespace assets { class AssetResolver; }
    struct TextRenderResources;
}

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d::backends::text {
    // TICKET-011d — defined under include/chronon3d/backends/text/.
    class TextRasterService;
}

namespace chronon3d {

struct SoftwareBackendServices {
    class SoftwareRenderer*                          owner{nullptr};                 // REQUIRED
    RenderCounters*                                  counters{nullptr};              // REQUIRED
    const RenderSettings*                            settings{nullptr};              // REQUIRED
    std::shared_ptr<cache::FramebufferPool>         framebuffer_pool{nullptr};      // REQUIRED (shared)
    const assets::AssetResolver*                     asset_resolver{nullptr};        // REQUIRED
    TextRenderResources*                             text_resources{nullptr};        // REQUIRED
    ImageRenderer*                                   images{nullptr};                // optional
    chronon3d::backends::text::TextRasterService*   text_raster{nullptr};           // optional (TICKET-011d)
    const DebugConfig*                               debug_config{nullptr};          // optional
};

// ═════════════════════════════════════════════════════════════════════════════
//  SoftwareBackendServicesError — typed failure payload from
//  `make_software_backend`.  Mirrors the project convention of
//  `ScopeError` (core/execution/execution_scope_types.hpp) and the
//  `sdk::RenderError` shape: categorical Code + free-form field name +
//  free-form message.  Used as the `E` parameter of
//  `Result<std::unique_ptr<SoftwareBackend>, SoftwareBackendServicesError>`.
// ═════════════════════════════════════════════════════════════════════════════

struct SoftwareBackendServicesError {
    enum class Code : std::uint8_t {
        MissingOwner            = 1,
        MissingCounters         = 2,
        MissingSettings         = 3,
        MissingFramebufferPool  = 4,
        MissingAssetResolver    = 5,
        MissingTextResources    = 6,
    };

    Code        code{Code::MissingOwner};
    std::string field_name;    // mirrors the SoftwareBackendServices field name
    std::string message;       // free-form, never empty
};

/// Stable string-form name for each `Code`.  For logging + diagnostics only.
inline const char* software_backend_services_error_name(
    SoftwareBackendServicesError::Code c) noexcept {
    switch (c) {
        case SoftwareBackendServicesError::Code::MissingOwner:
            return "MissingOwner";
        case SoftwareBackendServicesError::Code::MissingCounters:
            return "MissingCounters";
        case SoftwareBackendServicesError::Code::MissingSettings:
            return "MissingSettings";
        case SoftwareBackendServicesError::Code::MissingFramebufferPool:
            return "MissingFramebufferPool";
        case SoftwareBackendServicesError::Code::MissingAssetResolver:
            return "MissingAssetResolver";
        case SoftwareBackendServicesError::Code::MissingTextResources:
            return "MissingTextResources";
    }
    return "Unknown";
}

} // namespace chronon3d
