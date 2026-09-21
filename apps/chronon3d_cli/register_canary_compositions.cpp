// ============================================================================
// apps/chronon3d_cli/register_canary_compositions.cpp
//
// The Motion3D canary pack — entry point.
//
// These compositions exist to be *measured*, not admired: each one puts a
// different part of the engine under load in the way a real promo does, with
// the pieces stacked so a failure has one place to live.  Every composition is
// a pure function of `ctx.frame()`: the scene is rebuilt for the frame being
// rendered, so the same frame always yields the same scene regardless of what
// was rendered before it.  That is what makes the golden hashes in the canary
// harness meaningful.
//
// The pack is split by family, one subfile per family (see canary/), and this
// file is the only orchestrator.  The call order below IS the registry order,
// so it is part of the pack's observable output:
//
//   canary/register_canary_motion3d.cpp        the five Motion3D measured scenes
//   canary/register_canary_text.cpp            the Chronon text-element contract
//   canary/register_canary_final3d.cpp         the nine Final3D production IDs
//   canary/register_canary_font_baselines.cpp  the five static font/glow baselines
//   canary/register_canary_apple_v3.cpp        the 5×5 Apple V3 choreography matrix
//   canary/register_canary_final_cards.cpp     the typewriter and smooth-orbit cards
//
// The shared authoring kit lives in canary/canary_common.hpp (constants,
// timing, text shape, orbit camera, specs) and canary/canary_text_animation.hpp
// (typewriter and element definitions); each subfile's own header comment
// explains what its family is evidence of.
// ============================================================================

#include "register_compositions.hpp"

#include "canary/register_canary.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d {

    void register_canary_compositions(CompositionRegistry &registry) {

        register_canary_motion3d_compositions(registry);
        register_canary_text_compositions(registry);
        register_canary_final3d_compositions(registry);
        register_canary_font_baseline_compositions(registry);
        register_canary_apple_v3_compositions(registry);
        register_canary_final_card_compositions(registry);
    }

} // namespace chronon3d
