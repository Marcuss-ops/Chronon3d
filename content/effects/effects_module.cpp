#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::effects {

// ── Glow compositions ───────────────────────────────────────────────────────
Composition glow_02_orb_galaxy();
Composition glow_basic_word();
// Composition glow_03_cinematic_text();  // removed — fresh start
Composition glow_sharpness_test();
Composition glow_paragraph_test();
Composition glow_radius_compare_test();
Composition glow_typewriter_reveal_test();
Composition glow_shadow_balance_test();

// ── 2.5D reference compositions ─────────────────────────────────────────────
Composition floating_cards_test();
Composition orbit_camera_test();
Composition depth_fog_test();
Composition z_stack_parallax_test();
Composition shadow_glow_consistency_test();
Composition extreme_perspective_test();
Composition y_rotation_text_test();

// ── Thumbnail compositions ──────────────────────────────────────────────────
Composition premium_thumbnail_buttery_smooth();
Composition premium_thumbnail_saas_blue();

} // namespace chronon3d::content::effects

namespace chronon3d {

class EffectsModule : public ExtensionModule {
public:
    std::string_view id() const override { return "effects"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::effects;

        // Glow compositions kept in the module
        registry.register_composition("GlowOrbGalaxy",         glow_02_orb_galaxy);
        registry.register_composition("GlowBasicWord",         glow_basic_word);

        // V2 glow A/B tests
        registry.register_composition("GlowSharpnessTest",        glow_sharpness_test);
        registry.register_composition("GlowParagraphTest",        glow_paragraph_test);
        registry.register_composition("GlowRadiusCompareTest",    glow_radius_compare_test);
        registry.register_composition("GlowTypewriterRevealTest", glow_typewriter_reveal_test);
        registry.register_composition("GlowShadowBalanceTest",    glow_shadow_balance_test);

        // 2.5D reference suite
        registry.register_composition("FloatingCardsTest",                floating_cards_test);
        registry.register_composition("OrbitCameraTest",                  orbit_camera_test);
        registry.register_composition("DepthFogTest",                    depth_fog_test);
        registry.register_composition("ZStackParallaxTest",              z_stack_parallax_test);
        registry.register_composition("ShadowGlowConsistencyTest",       shadow_glow_consistency_test);
        registry.register_composition("ExtremePerspectiveTest",          extreme_perspective_test);
        registry.register_composition("YRotationTextTest",               y_rotation_text_test);

        // Thumbnail compositions
        registry.register_composition("PremiumThumbnailButterySmooth",   premium_thumbnail_buttery_smooth);
        registry.register_composition("PremiumThumbnailSaaSBlue",        premium_thumbnail_saas_blue);
    }
};

std::unique_ptr<ExtensionModule> create_effects_module() {
    return std::make_unique<EffectsModule>();
}

} // namespace chronon3d
