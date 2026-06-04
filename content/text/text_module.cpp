#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::text {

// ── Forward declarations (definitions in separate .cpp files) ──────────

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
Composition text_typewriter_terminal_preview();
Composition text_typewriter_quote();
Composition text_typewriter_quote_preview();
Composition text_typewriter_manifest();
Composition text_typewriter_manifest_preview();
Composition text_typewriter_chapter();
Composition text_typewriter_chapter_preview();
Composition text_typewriter_showcase();
Composition text_typewriter_animation_test();
Composition text_typewriter_dolly();
Composition text_typewriter_dolly_rotate();
Composition text_typewriter_stagger();
Composition text_typewriter_bounce();
Composition text_typewriter_glitch();
Composition text_typewriter_push();
Composition text_typewriter_slide();
Composition text_typewriter_reveal_sweep();
Composition text_intro_clean_reveal();
Composition text_intro_sweep_reveal();
Composition text_intro_impact_pulse();
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
Composition text_hero_fresh();
Composition text_empty();
Composition text_only();
Composition text_proofs();
Composition text_premium_hero();
Composition text_premium_hero_saas_blue();
Composition text_premium_hero_explainer();
Composition text_premium_hero_buttery_smooth();
Composition lil_dirk();

} // namespace chronon3d::content::text

namespace chronon3d {

class TextModule : public ExtensionModule {
public:
    std::string_view id() const override { return "text"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::text;

        // Core text presets
        registry.register_composition("TextCreditRoll", text_credit_roll);
        registry.register_composition("TextSplitScreen", text_split_screen);
        registry.register_composition("TextGlow", text_glow);
        registry.register_composition("TextShadow", text_shadow);
        registry.register_composition("TextPulse", text_pulse);
        registry.register_composition("TextMultiStyle", text_multi_style);
        registry.register_composition("TextOnBackground", text_on_background);
        registry.register_composition("TextGridOverlay", text_grid_overlay);

        // Typewriter variants
        registry.register_composition("TextTypewriter", text_typewriter);
        registry.register_composition("TextTypewriterTerminal", text_typewriter_terminal);
        registry.register_composition("TextTypewriterTerminalPreview", text_typewriter_terminal_preview);
        registry.register_composition("TextTypewriterQuote", text_typewriter_quote);
        registry.register_composition("TextTypewriterQuotePreview", text_typewriter_quote_preview);
        registry.register_composition("TextTypewriterManifest", text_typewriter_manifest);
        registry.register_composition("TextTypewriterManifestPreview", text_typewriter_manifest_preview);
        registry.register_composition("TextTypewriterChapter", text_typewriter_chapter);
        registry.register_composition("TextTypewriterChapterPreview", text_typewriter_chapter_preview);
        registry.register_composition("TextTypewriterShowcase", text_typewriter_showcase);
        registry.register_composition("TextTypewriterAnimationTest", text_typewriter_animation_test);
        registry.register_composition("TextTypewriterDolly", text_typewriter_dolly);
        registry.register_composition("TextTypewriterDollyRotate", text_typewriter_dolly_rotate);
        registry.register_composition("TextTypewriterStagger", text_typewriter_stagger);
        registry.register_composition("TextTypewriterBounce", text_typewriter_bounce);
        registry.register_composition("TextTypewriterGlitch", text_typewriter_glitch);
        registry.register_composition("TextTypewriterPush", text_typewriter_push);
        registry.register_composition("TextTypewriterSlide", text_typewriter_slide);
        registry.register_composition("TextTypewriterRevealSweep", text_typewriter_reveal_sweep);

        // Intro variants
        registry.register_composition("TextIntroCleanReveal", text_intro_clean_reveal);
        registry.register_composition("TextIntroSweepReveal", text_intro_sweep_reveal);
        registry.register_composition("TextIntroImpactPulse", text_intro_impact_pulse);

        // Motion / demo
        registry.register_composition("TextAnimatedSequence", text_animated_sequence);
        registry.register_composition("TextCountdown", text_countdown);
        registry.register_composition("TextFadeLiftDemo", text_fade_lift_demo);
        registry.register_composition("TextSoftDollyRevealDemo", text_soft_dolly_reveal_demo);
        registry.register_composition("TextMaskSweepDemo", text_mask_sweep_demo);
        registry.register_composition("TextFocusPullDemo", text_focus_pull_demo);
        registry.register_composition("TextGlowBloomDemo", text_glow_bloom_demo);
        registry.register_composition("TextStaggerRevealDemo", text_stagger_reveal_demo);
        registry.register_composition("TextOrbit2_5DDemo", text_orbit_2_5d_demo);
        registry.register_composition("TextTiltSweep2_5DDemo", text_tilt_sweep_2_5d_demo);
        registry.register_composition("TextMotionTrioDemo", text_motion_trio_demo);

        // Premium hero
        registry.register_composition("HeroFresh", text_hero_fresh);
        registry.register_composition("TextPremiumHero", text_premium_hero);
        registry.register_composition("TextPremiumHeroSaaSBlue", text_premium_hero_saas_blue);
        registry.register_composition("TextPremiumHeroExplainer", text_premium_hero_explainer);
        registry.register_composition("TextPremiumHeroButterySmooth", text_premium_hero_buttery_smooth);

        // Utility
        registry.register_composition("Empty", text_empty);
        registry.register_composition("TextOnly", text_only);
        registry.register_composition("TextProofs", text_proofs);

        // Standalone
        registry.register_composition("LilDirk", lil_dirk);
    }
};

std::unique_ptr<ExtensionModule> create_text_module() {
    return std::make_unique<TextModule>();
}

} // namespace chronon3d
