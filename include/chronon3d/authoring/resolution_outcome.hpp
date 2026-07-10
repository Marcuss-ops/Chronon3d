// ═══════════════════════════════════════════════════════════════════════════
// resolution_outcome.hpp — explicit tracking for style/motion resolution.
//
// A3 — structured registry errors.  Replaces the previous silent no-op
// contract where `.style("hero-title")` could compile and render without
// any style applied and the user received zero feedback.
//
// Three explicit outcomes replace the single implicit "no-op" state:
//
//   Found                — id resolved successfully, style/motion applied.
//   Missing              — id not found in the registry.
//   RegistryUnavailable  — ambient registry pointer is null (no
//                          ExtensionContext attached, or the host didn't
//                          populate the style_registry/motion_registry slot).
//
// The `NotAttempted` sentinel is the default-initialised state before any
// `.style()` / `.motion()` call is made on the Text handle.  It lets
// callers distinguish "never called" from "called but failed".
//
// Usage from tests:
//   CHECK(t.last_style_outcome() == ResolutionOutcome::Found);
//   CHECK(t.last_style_outcome() == ResolutionOutcome::Missing);
//
// Preflight integration (future): the preflight pass walks every
// PendingTextRun, inspects its outcome, and surfaces Missing /
// RegistryUnavailable as structured diagnostics.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

namespace chronon3d::authoring {

enum class ResolutionOutcome {
    NotAttempted,         // No .style() / .motion() call has been made yet
    Found,                // Successfully resolved from the registry
    Missing,              // ID not found in the registry
    RegistryUnavailable,  // Ambient registry pointer is null
};

} // namespace chronon3d::authoring
