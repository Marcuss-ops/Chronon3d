#pragma once
// tests/visual/glow_ab/glow_ab_compositions.hpp
//
// BUG 2 / TICKET-TEXT-GLOW-DARKENING — Step 3 (A/B test).
//
// Sibling composition to `content/examples/text/text_animations.cpp::anim_typewriter_glow`
// that mirrors the SAME scene but with `glow_intensity=0.0` (control case).
//
// Why a sibling instead of a CLI flag on the existing composition:
//   - The user constraint is "NON toccare il codice di produzione".
//   - anim_typewriter_glow() is wired into animation_compositions.cpp; touching
//     its signature or runtime would break existing content registrations.
//   - A new composition that calls build_2line_typewriter with the SAME
//     parameters except `glow_intensity=0.0` produces a true A/B pair with
//     only the glow variable changed — pixel-identical scene except for the
//     glow halo (or absence thereof).
//
// This file declares ONLY the factory functions; the definition lives in
// glow_ab_compositions.cpp.  Registration into the CLI is performed via
// `register_glow_ab_compositions(CompositionRegistry&)` — called from
// apps/chronon3d_cli/cli_init.hpp with a 1-line additive include + 1-line
// registry.add() (the only edit to a production file required to enable
// the experiment, documented as such in the baseline report).

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d::test::glow_ab {

/// Composition that replicates `anim_typewriter_glow` with `glow_intensity=0.0`.
/// Same scene (text, font, layout, timing, drop shadow, background) — only the
/// glow variable is changed (off).  Use as the A/B "without glow" partner.
chronon3d::Composition make_anim_typewriter_glow_no_glow();

/// Register the glow A/B compositions into a registry.  Idempotent.
void register_glow_ab_compositions(chronon3d::CompositionRegistry& registry);

} // namespace chronon3d::test::glow_ab
