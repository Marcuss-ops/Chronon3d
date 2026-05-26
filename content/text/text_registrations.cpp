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
Composition text_animated_sequence();
Composition text_countdown();
Composition text_premium_motion_pack();
// Composition text_cinematic_intro();
Composition text_perspective_sweep_demo();
// Composition text_glow_transition();
// Composition text_spring_reveal();
// Composition text_cyberpunk_glitch();
// Composition text_credits_3d();

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
CHRONON_REGISTER_COMPOSITION("TextAnimatedSequence",chronon3d::content::text::text_animated_sequence)
CHRONON_REGISTER_COMPOSITION("TextCountdown",       chronon3d::content::text::text_countdown)
CHRONON_REGISTER_COMPOSITION("TextPremiumMotionPack", chronon3d::content::text::text_premium_motion_pack)
// CHRONON_REGISTER_COMPOSITION("TextCinematicIntro",  chronon3d::content::text::text_cinematic_intro)
CHRONON_REGISTER_COMPOSITION("TextPerspectiveSweepDemo", chronon3d::content::text::text_perspective_sweep_demo)
// CHRONON_REGISTER_COMPOSITION("TextGlowTransition",  chronon3d::content::text::text_glow_transition)
// CHRONON_REGISTER_COMPOSITION("TextSpringReveal",    chronon3d::content::text::text_spring_reveal)
// CHRONON_REGISTER_COMPOSITION("TextCyberpunkGlitch", chronon3d::content::text::text_cyberpunk_glitch)
// CHRONON_REGISTER_COMPOSITION("TextCredits3D",       chronon3d::content::text::text_credits_3d)
