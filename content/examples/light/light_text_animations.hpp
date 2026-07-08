#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d { class CompositionRegistry; }

namespace chronon3d::content::light_text {

// ── 8 Light 2D Text Entrance Animations ────────────────────────────────
// Each animation uses a single text layer with keyframe-based animation
// on a MinimalistGrid background. No per-glyph typewriter (avoids the
// TICKET-104 framebuffer crash with per-character layers).
//
// 1. LightPulse     — gentle scale pulse with fade in
// 2. LightWobble    — rotation wobble that settles
// 3. LightDropSpring — drops from above with spring bounce
// 4. LightGlideBlur — glides from left while blur clears
// 5. LightRevealX   — horizontal stretch reveal
// 6. LightFloatUp   — floats up from below with scale
// 7. LightSpin      — spins in with rotation
// 8. LightGlowPulse — glow-like blur pulse with fade

Composition light_pulse();
Composition light_wobble();
Composition light_drop_spring();
Composition light_glide_blur();
Composition light_reveal_x();
Composition light_float_up();
Composition light_spin();
Composition light_glow_pulse();

/// Register all LightText compositions into the given registry.
void register_light_text_compositions(chronon3d::CompositionRegistry& registry);

} // namespace chronon3d::content::light_text
