// content/effects/glow_02_orb_galaxy.cpp
// TEST 2 — Orb Galaxy: 7 colored orbs with different radii on starfield
#include "../common/glow_test_common.hpp"
#include <chronon3d/core/composition/composition_registration.hpp>

namespace chronon3d::content::effects {

Composition glow_02_orb_galaxy() {
    return composition({.name="GlowOrbGalaxy",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.02f,0.01f,0.06f,1}, Color{0.01f,0.01f,0.02f,1});

        // Star field
        s.layer("stars", [](LayerBuilder& l) {
            const float sx[]={-840,-680,-520,-360,-200,-40,120,280,440,600,760,
                              -720,-400,-80,240,560,880,-560,160,480,-320};
            const float sy[]={-380,-260,-420,-300,-440,-360,-380,-280,-420,-340,
                              -260,-390,-150,-200,-180,-160,-140,-440,-460,-480,-300};
            for (int i = 0; i < 21; ++i)
                l.circle("s"+std::to_string(i), {
                    .radius=1.5f+0.8f*(i%4),
                    .color=Color{1,1,1,0.25f+0.15f*(i%4)},
                    .pos={sx[i],sy[i],0}
                });
        });

        // 7 orbs — varied colors, sizes, radii, intensities
        struct OrbDef { float x,y,r,rad,inten; Color col; };
        const OrbDef orbs[]={
            {-700,-80,  40, 65, 1.5f, Color{0.20f,0.60f,1.0f,1}},  // blue
            {-350, 60,  30, 42, 1.1f, Color{0.90f,0.28f,1.0f,1}},  // purple
            {   0,-200, 50, 80, 1.8f, Color{0.22f,0.95f,0.72f,1}}, // green
            { 280, 100, 24, 34, 0.9f, Color{1.0f, 0.56f,0.18f,1}}, // orange
            { 560,-80,  36, 56, 1.3f, Color{1.0f, 0.24f,0.52f,1}}, // pink
            {-560, 200, 20, 28, 0.8f, Color{0.60f,0.88f,1.0f,1}},  // light blue
            { 180, 260, 28, 40, 1.0f, Color{1.0f, 0.82f,0.20f,1}}, // gold
        };

        for (int i = 0; i < 7; ++i) {
            const int idx = i;
            s.layer("orb"+std::to_string(idx), [orbs,idx](LayerBuilder& l) {
                const auto& o = orbs[idx];
                l.position({o.x, o.y, 0})
                 .glow(GlowParams{.radius = o.rad, .intensity = o.inten, .color = o.col});
                l.circle("c", {.radius=o.r,.color=Color{1,1,1,0.95f},.pos={0,0,0}});
            });
        }

        // Labels under each orb
        const char* labels[]={"R65 I1.5","R42 I1.1","R80 I1.8","R34 I0.9","R56 I1.3","R28 I0.8","R40 I1.0"};
        for (int i = 0; i < 7; ++i) {
            const int idx = i;
            s.layer("lbl"+std::to_string(idx), [orbs,labels,idx](LayerBuilder& l) {
                l.position({orbs[idx].x, orbs[idx].y + orbs[idx].r + 40.f, 0});
                l.text("t", {
                    .text=labels[idx], .size={140,32}, .pos={0,0,0},
                    .font_size=14.f, .color=Color{0.65f,0.72f,0.85f,1},
                    .anchor=TextAnchor::Center, .align=TextAlign::Center, .vertical_align=VerticalAlign::Middle
                });
            });
        }

        bottom_label(s, "TEST 2 — Orb Galaxy: 7 colored glowing orbs, varied radius & intensity");
        return s.build();
    });
}

// Forward-declare factories from companion files
Composition glow_basic_word();
Composition glow_sharpness_test();
Composition glow_paragraph_test();
Composition glow_radius_compare_test();
Composition glow_typewriter_reveal_test();
Composition glow_shadow_balance_test();
Composition premium_thumbnail_buttery_smooth();
Composition premium_thumbnail_saas_blue();
Composition floating_cards_test();
Composition orbit_camera_test();
Composition depth_fog_test();
Composition z_stack_parallax_test();
Composition shadow_glow_consistency_test();
Composition extreme_perspective_test();
Composition y_rotation_text_test();

// ── Per-domain registration ──────────────────────────────────────────────────
void register_effect_compositions() {
    static bool done = false;
    if (done) return;
    done = true;
    detail::add_builtin_composition("GlowOrbGalaxy",              glow_02_orb_galaxy);
    detail::add_builtin_composition("GlowBasicWord",              glow_basic_word);
    detail::add_builtin_composition("GlowSharpnessTest",          glow_sharpness_test);
    detail::add_builtin_composition("GlowParagraphTest",          glow_paragraph_test);
    detail::add_builtin_composition("GlowRadiusCompareTest",      glow_radius_compare_test);
    detail::add_builtin_composition("GlowTypewriterRevealTest",   glow_typewriter_reveal_test);
    detail::add_builtin_composition("GlowShadowBalanceTest",      glow_shadow_balance_test);
    detail::add_builtin_composition("PremiumThumbnailButterySmooth", premium_thumbnail_buttery_smooth);
    detail::add_builtin_composition("PremiumThumbnailSaaSBlue",   premium_thumbnail_saas_blue);
    detail::add_builtin_composition("FloatingCardsTest",          floating_cards_test);
    detail::add_builtin_composition("OrbitCameraTest",            orbit_camera_test);
    detail::add_builtin_composition("DepthFogTest",               depth_fog_test);
    detail::add_builtin_composition("ZStackParallaxTest",         z_stack_parallax_test);
    detail::add_builtin_composition("ShadowGlowConsistencyTest",  shadow_glow_consistency_test);
    detail::add_builtin_composition("ExtremePerspectiveTest",     extreme_perspective_test);
    detail::add_builtin_composition("YRotationTextTest",          y_rotation_text_test);
}

} // namespace chronon3d::content::effects

