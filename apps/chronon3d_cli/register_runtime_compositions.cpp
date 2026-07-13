// ============================================================================
// apps/chronon3d_cli/register_runtime_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — production runtime composition surface.
// Always called (production CLI + DEV CLI).
//
// Registers:
//   1. chronon3d::register_builtin_compositions(registry) — DarkGridBackground,
//      CameraImageClip (the canonical "always available" production surfaces).
//   2. ChrononGlowFinalAE + ChrononGlowFinalAEPortrait — the canonical
//      user-spec cinematic-glow compositions (landscape + portrait).
//      Single canonical authority per AR (16:9 vs 9:16); the legacy alias
//      `chronon-glow-final` has been REMOVED (Action 15, TICKET-CHRONON-
//      GLOW-FINAL-DEDUP-PROPS forward-point closure).
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
    // PRODUCTION entry point; the NoGlow sibling lives in
    // register_dev_compositions (DEV-gated, A/B acceptance).
    //
    // Single Source of Truth (Step 8 §A + Action 15 closure): 2 production
    // entries, each backed by a SEPARATE lambda (different default props
    // — landscape uses default_landscape_props(), portrait uses
    // default_portrait_props()).  The legacy alias `chronon-glow-final`
    // is REMOVED — it duplicated the landscape composition under a
    // different id (Action 15 forward-point of TICKET-CHRONON-GLOW-FINAL-
    // DEDUP-PROPS closure lineage).
    auto make_landscape_comp = [](const CompositionProps&) -> Composition {
        return chronon3d::content::glow_final::make_chronon_glow_final(
            chronon3d::content::glow_final::default_landscape_props());
    };
    registry.add("ChrononGlowFinalAE", make_landscape_comp);
    registry.add("ChrononGlowFinalAEPortrait",
        [](const CompositionProps&) -> Composition {
            return chronon3d::content::glow_final::make_chronon_glow_final(
                chronon3d::content::glow_final::default_portrait_props());
        });
}

} // namespace chronon3d
