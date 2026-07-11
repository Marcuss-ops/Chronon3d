#pragma once
// tests/visual/glow_ab/glow_ab_compositions.hpp
//
// BUG 2 / TICKET-TEXT-GLOW-DARKENING — Step 3 (A/B test).
//
// Sibling composition to `content/examples/text/text_animations.cpp::anim_typewriter_glow`
// that mirrors the SAME scene with a configurable `glow_intensity` parameter.
// Call the factory once with 0.0f (no-glow control) and once with 0.5f
// (with-glow match to production) to get the A/B pair from the same code path.
//
// Why a sibling instead of a CLI flag on the existing composition:
//   - The user constraint is "NON toccare il codice di produzione".
//   - anim_typewriter_glow() is wired into animation_compositions.cpp; touching
//     its signature or runtime would break existing content registrations.
//   - A new composition that calls build_2line_typewriter with the SAME
//     parameters except `glow_intensity` produces a true A/B pair with
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

/// Composition factory that mirrors `anim_typewriter_glow` with a configurable
/// `glow_intensity`.  Call once with `glow_intensity=0.0f` for the no-glow
/// A/B control and once with `glow_intensity=0.5f` to match the production
/// WITH-glow behavior — same scene (text, font, layout, timing, drop shadow,
/// background), only the glow variable changes.
///
/// The composition's internal name is derived from `glow_intensity` so the
/// two variants are distinguishable in logs (0.0 → "AnimTypewriterGlowNoGlow",
/// >0.0 → "AnimTypewriterGlowWithGlow").  The registry key passed to
/// `register_glow_ab_compositions` is the canonical CLI name.
chronon3d::Composition make_anim_typewriter_glow_no_glow(f32 glow_intensity);

/// Register the glow A/B compositions into a registry.  Idempotent.
/// Adds two entries: `AnimTypewriterGlowNoGlow` (glow_intensity=0.0f) and
/// `AnimTypewriterGlowWithGlow` (glow_intensity=0.5f).
void register_glow_ab_compositions(chronon3d::CompositionRegistry& registry);

} // namespace chronon3d::test::glow_ab
