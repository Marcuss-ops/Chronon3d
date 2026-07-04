// ============================================================================
// src/backends/software/software_backend_factory.cpp
//
// M1.5#12 1/4 — make_software_backend() factory extraction.
//
// Single source of truth for `SoftwareBackendServices` REQUIRED-field
// validation.  The factory is the ONLY place that returns a malformed
// services-bundle error; the direct SoftwareBackend(...) ctor in
// software_backend.cpp is intentionally unvalidated so callers observe
// the failure at first dispatch if they bypass the factory.
//
// Anti-duplication invariant (strict): NO debug-time `assert` repeats
// in this body — release-path `if (...) return err(...)` IS the unique
// validation.  Caller intent is opt-in via the return value.
// Full architecture rationale: docs/tickets/TICKET-M1.5#12-SEQUENCE.md.
// ============================================================================

#include <chronon3d/backends/software/software_backend.hpp>

// ═════════════════════════════════════════════════════════════════════════════
//  make_software_backend — validation-gate factory.
// ═════════════════════════════════════════════════════════════════════════════

namespace chronon3d {

Result<std::unique_ptr<SoftwareBackend>, SoftwareBackendServicesError>
make_software_backend(SoftwareBackendServices services) {
    // Helper: build a structured SoftwareBackendServicesError for a given
    // Code + field name.  The message includes both the field name and
    // the user-ordered field list so logs read deterministic.
    auto err = [](SoftwareBackendServicesError::Code c,
                  const char* field) -> SoftwareBackendServicesError {
        return SoftwareBackendServicesError{
            c,
            field,
            std::string{"make_software_backend: required service '"} + field +
                "' is null (SoftwareBackendServices is malformed)"
        };
    };

    // ── Debug guards: assert on first failure ──
    // Fires unconditionally in NDEBUG=0 so a caller that forgets to
    // inspect the Result surfaces the failure in the debugger before
    // silently proceeding with a malformed backend.
    // Canonical assert form: assert(ptr && "msg") keeps the static analyzer
    // quiet on the pointer value while carrying a descriptive label in
    // the diagnostic on failure.
    // no longer exists on SoftwareBackendServices; processors are wired
    // post-construction via attach_processor_context().

    // ── Release path: ordered validation matches the user spec:
    //    counters → settings → framebuffer_pool → asset_resolver → text_resources.
    // TICKET-118 — MissingOwner branch REMOVED.  Renumbered Code enum values.
    if (services.counters == nullptr)
        return err(SoftwareBackendServicesError::Code::MissingCounters,          "counters");
    if (services.settings == nullptr)
        return err(SoftwareBackendServicesError::Code::MissingSettings,          "settings");
    if (!services.framebuffer_pool)
        return err(SoftwareBackendServicesError::Code::MissingFramebufferPool,   "framebuffer_pool");
    if (services.asset_resolver == nullptr)
        return err(SoftwareBackendServicesError::Code::MissingAssetResolver,     "asset_resolver");
    if (services.text_resources == nullptr)
        return err(SoftwareBackendServicesError::Code::MissingTextResources,     "text_resources");

    return std::make_unique<SoftwareBackend>(std::move(services));
}

} // namespace chronon3d
