// ============================================================================
// apps/chronon3d_cli/register_runtime_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — production runtime composition surface.
// Always called (production CLI + DEV CLI).
//
// Registers:
//   1. chronon3d::register_builtin_compositions(registry) — DarkGridBackground,
//      CameraImageClip (the canonical "always available" production surfaces).
//   2. ChrononGlowFinalAE — the canonical user-spec cinematic-glow
//      composition (production name; alias for `chronon-glow-final`).
//
// Cat-3 minimal-surface: consumes the existing
// register_builtin_compositions.hpp public surface + the header-only
// content/compositions/chronon_glow_final.hpp production factory.
// No tests/visual/ headers, no DEV-only surfaces.
// ============================================================================

#include "register_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/timeline/composition.hpp>

// TICKET-CHRONON-GLOW-FINAL — Phase 1 unified cinematic glow factory.
// Header-only inline factory; included transitively from CMAKE_SOURCE_DIR.
// Available in production (no DEV/test gating).
#include "content/compositions/chronon_glow_final.hpp"

namespace chronon3d {

void register_runtime_compositions(CompositionRegistry& registry) {
    // (1) Built-in compositions (DarkGridBackground, CameraImageClip).
    chronon3d::register_builtin_compositions(registry);

    // (2) ChrononGlowFinalAE — the canonical user-spec name for the
    // cinematic-glow landscape composition.  Routes through the Phase 1
    // unified helper (header-only; no link-time surface).  This is the
    // PRODUCTION entry point; the no-glow + portrait siblings live in
    // register_dev_compositions (DEV-gated, A/B acceptance + portrait).
    registry.add("ChrononGlowFinalAE",
        [](const CompositionProps&) -> Composition {
            ChrononGlowProps p = chronon3d::test::glow_final::default_landscape_props();
            return chronon3d::test::glow_final::make_chronon_glow_final(p);
        });
}

} // namespace chronon3d
