#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::text {

// Removed/cleaned up:
// Composition text_title_big();
// Composition text_subtitle();
// Composition text_lower_third();
Composition text_credit_roll();
Composition text_split_screen();
Composition text_glow();
Composition text_shadow();
Composition text_pulse();
Composition text_multi_style();
Composition text_on_background();
Composition text_grid_overlay();
Composition text_typewriter();
Composition text_typewriter_terminal();
Composition text_typewriter_quote();
Composition text_typewriter_manifest();
Composition text_typewriter_chapter();
Composition text_typewriter_dolly();
Composition text_typewriter_stagger();
Composition text_typewriter_bounce();
Composition text_typewriter_glitch();
Composition text_typewriter_push();
Composition text_typewriter_slide();
Composition text_typewriter_reveal_sweep();
Composition text_animated_sequence();
Composition text_countdown();
Composition text_fade_lift_demo();
Composition text_soft_dolly_reveal_demo();
Composition text_mask_sweep_demo();
Composition text_focus_pull_demo();
Composition text_glow_bloom_demo();
Composition text_stagger_reveal_demo();
Composition text_orbit_2_5d_demo();
Composition text_tilt_sweep_2_5d_demo();
Composition text_motion_trio_demo();
Composition text_proofs();

void register_all() {}

} // namespace chronon3d::content::text

// CHRONON_REGISTER_COMPOSITION("TextTitleBig",        chronon3d::content::text::text_title_big)
// CHRONON_REGISTER_COMPOSITION("TextSubtitle",        chronon3d::content::text::text_subtitle)
// CHRONON_REGISTER_COMPOSITION("TextLowerThird",      chronon3d::content::text::text_lower_third)
CHRONON_REGISTER_COMPOSITION("TextCreditRoll",      chronon3d::content::text::text_credit_roll)
CHRONON_REGISTER_COMPOSITION("TextSplitScreen",     chronon3d::content::text::text_split_screen)
CHRONON_REGISTER_COMPOSITION("TextGlow",            chronon3d::content::text::text_glow)
CHRONON_REGISTER_COMPOSITION("TextShadow",          chronon3d::content::text::text_shadow)
CHRONON_REGISTER_COMPOSITION("TextPulse",           chronon3d::content::text::text_pulse)
CHRONON_REGISTER_COMPOSITION("TextMultiStyle",      chronon3d::content::text::text_multi_style)
CHRONON_REGISTER_COMPOSITION("TextOnBackground",    chronon3d::content::text::text_on_background)
CHRONON_REGISTER_COMPOSITION("TextGridOverlay",     chronon3d::content::text::text_grid_overlay)
CHRONON_REGISTER_COMPOSITION("TextTypewriter",      chronon3d::content::text::text_typewriter)
CHRONON_REGISTER_COMPOSITION("TextTypewriterTerminal", chronon3d::content::text::text_typewriter_terminal)
CHRONON_REGISTER_COMPOSITION("TextTypewriterQuote",    chronon3d::content::text::text_typewriter_quote)
CHRONON_REGISTER_COMPOSITION("TextTypewriterManifest",  chronon3d::content::text::text_typewriter_manifest)
CHRONON_REGISTER_COMPOSITION("TextTypewriterChapter",   chronon3d::content::text::text_typewriter_chapter)
CHRONON_REGISTER_COMPOSITION("TextTypewriterDolly",     chronon3d::content::text::text_typewriter_dolly)
CHRONON_REGISTER_COMPOSITION("TextTypewriterStagger",   chronon3d::content::text::text_typewriter_stagger)
CHRONON_REGISTER_COMPOSITION("TextTypewriterBounce",    chronon3d::content::text::text_typewriter_bounce)
CHRONON_REGISTER_COMPOSITION("TextTypewriterGlitch",    chronon3d::content::text::text_typewriter_glitch)
CHRONON_REGISTER_COMPOSITION("TextTypewriterPush",      chronon3d::content::text::text_typewriter_push)
CHRONON_REGISTER_COMPOSITION("TextTypewriterSlide",     chronon3d::content::text::text_typewriter_slide)
CHRONON_REGISTER_COMPOSITION("TextTypewriterRevealSweep", chronon3d::content::text::text_typewriter_reveal_sweep)
CHRONON_REGISTER_COMPOSITION("TextAnimatedSequence",chronon3d::content::text::text_animated_sequence)
CHRONON_REGISTER_COMPOSITION("TextCountdown",       chronon3d::content::text::text_countdown)
CHRONON_REGISTER_COMPOSITION("TextFadeLiftDemo",     chronon3d::content::text::text_fade_lift_demo)
CHRONON_REGISTER_COMPOSITION("TextSoftDollyRevealDemo", chronon3d::content::text::text_soft_dolly_reveal_demo)
CHRONON_REGISTER_COMPOSITION("TextMaskSweepDemo",    chronon3d::content::text::text_mask_sweep_demo)
CHRONON_REGISTER_COMPOSITION("TextFocusPullDemo",     chronon3d::content::text::text_focus_pull_demo)
CHRONON_REGISTER_COMPOSITION("TextGlowBloomDemo",     chronon3d::content::text::text_glow_bloom_demo)
CHRONON_REGISTER_COMPOSITION("TextStaggerRevealDemo", chronon3d::content::text::text_stagger_reveal_demo)
CHRONON_REGISTER_COMPOSITION("TextOrbit2_5DDemo",    chronon3d::content::text::text_orbit_2_5d_demo)
CHRONON_REGISTER_COMPOSITION("TextTiltSweep2_5DDemo", chronon3d::content::text::text_tilt_sweep_2_5d_demo)
CHRONON_REGISTER_COMPOSITION("TextMotionTrioDemo",   chronon3d::content::text::text_motion_trio_demo)
CHRONON_REGISTER_COMPOSITION("TextProofs",          chronon3d::content::text::text_proofs)
