#include <doctest/doctest.h>
#include <chronon3d/presets/scene_presets.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/scene.hpp>

using namespace chronon3d;

// ── Helper ────────────────────────────────────────────────────────────────────

static Scene evaluate_preset(const Composition& comp, Frame frame) {
    return comp.evaluate(frame, std::pmr::get_default_resource());
}

// ── SaaSIntro ─────────────────────────────────────────────────────────────────

TEST_CASE("scene_presets::saas_intro builds successfully") {
    auto comp = scene_presets::saas_intro();

    CHECK(comp.name() == "SaaSIntro");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.duration() == 120);

    // Evaluate at several frames to ensure stability
    Scene s0 = evaluate_preset(comp, Frame{0});
    CHECK(s0.layers().size() > 0);

    Scene s30 = evaluate_preset(comp, Frame{30});
    CHECK(s30.layers().size() > 0);

    Scene s60 = evaluate_preset(comp, Frame{60});
    CHECK(s60.layers().size() > 0);

    Scene s_end = evaluate_preset(comp, Frame{119});
    CHECK(s_end.layers().size() > 0);
}

TEST_CASE("scene_presets::saas_intro has staggered cards") {
    auto comp = scene_presets::saas_intro();
    Scene s = evaluate_preset(comp, Frame{30});

    // Should have title layers (12 chars) + subtitle + 3 cards + CTA + bg + accents = ~20 layers
    // But at frame 30, cards may not be active yet if staggered
    int card_count = 0;
    for (const auto& layer : s.layers()) {
        if (layer.name.find("card_") == 0) {
            ++card_count;
            // Each card should have animated keyframes
            CHECK(layer.anim_transform.scale.is_animated());
            CHECK(layer.anim_transform.opacity.is_animated());
        }
    }
    CHECK(card_count == 3);
}

// ── FloatingCardsHero ─────────────────────────────────────────────────────────

TEST_CASE("scene_presets::floating_cards_hero builds successfully") {
    auto comp = scene_presets::floating_cards_hero();

    CHECK(comp.name() == "FloatingCardsHero");
    CHECK(comp.duration() == 150);

    // Evaluate at multiple frames
    Scene s_mid = evaluate_preset(comp, Frame{75});
    CHECK(s_mid.layers().size() > 0);

    Scene s_end = evaluate_preset(comp, Frame{149});
    CHECK(s_end.layers().size() > 0);
}

TEST_CASE("scene_presets::floating_cards_hero has 4 cards with 3D enabled") {
    auto comp = scene_presets::floating_cards_hero();
    Scene s = evaluate_preset(comp, Frame{60});

    int card_count = 0;
    for (const auto& layer : s.layers()) {
        if (layer.name.find("card_") == 0) {
            ++card_count;
            CHECK(layer.uses_2_5d_projection == true);
        }
    }
    CHECK(card_count == 4);
}

// ── KineticTitle ──────────────────────────────────────────────────────────────

TEST_CASE("scene_presets::kinetic_title builds successfully") {
    auto comp = scene_presets::kinetic_title();

    CHECK(comp.name() == "KineticTitle");
    CHECK(comp.duration() == 90);

    Scene s = evaluate_preset(comp, Frame{45});
    CHECK(s.layers().size() > 0);
}

TEST_CASE("scene_presets::kinetic_title has text layers with animations") {
    auto comp = scene_presets::kinetic_title();
    Scene s = evaluate_preset(comp, Frame{30});

    // Should have kinetic_title layers (13 chars) + emphasis + subtitle + bg + glow
    int anim_count = 0;
    for (const auto& layer : s.layers()) {
        if (layer.anim_transform.opacity.is_animated()) ++anim_count;
    }
    CHECK(anim_count >= 10);  // Most layers have animated opacity
}

// ── DepthProductReveal ────────────────────────────────────────────────────────

TEST_CASE("scene_presets::depth_product_reveal builds successfully") {
    auto comp = scene_presets::depth_product_reveal();

    CHECK(comp.name() == "DepthProductReveal");
    CHECK(comp.duration() == 120);

    Scene s = evaluate_preset(comp, Frame{60});
    CHECK(s.layers().size() > 0);
}

TEST_CASE("scene_presets::depth_product_reveal has 3D-enabled layers") {
    auto comp = scene_presets::depth_product_reveal();
    Scene s = evaluate_preset(comp, Frame{50});

    int three_d_count = 0;
    for (const auto& layer : s.layers()) {
        if (layer.uses_2_5d_projection) ++three_d_count;
    }
    // Product card + floating nodes + floor + atmosphere + hero_title
    CHECK(three_d_count >= 5);
}

// ── AppleStyleHero ────────────────────────────────────────────────────────────

TEST_CASE("scene_presets::apple_style_hero builds successfully") {
    auto comp = scene_presets::apple_style_hero();

    CHECK(comp.name() == "AppleStyleHero");
    CHECK(comp.duration() == 100);

    Scene s = evaluate_preset(comp, Frame{50});
    CHECK(s.layers().size() > 0);
}

TEST_CASE("scene_presets::apple_style_hero has CTA buttons") {
    auto comp = scene_presets::apple_style_hero();
    Scene s = evaluate_preset(comp, Frame{80});

    bool has_primary = false;
    bool has_secondary = false;
    for (const auto& layer : s.layers()) {
        if (layer.name == "cta_primary") has_primary = true;
        if (layer.name == "cta_secondary") has_secondary = true;
    }
    CHECK(has_primary);
    CHECK(has_secondary);
}

// ── Cross-preset checks ───────────────────────────────────────────────────────

TEST_CASE("All scene presets produce valid scenes at multiple frames") {
    auto presets = std::vector<std::pair<std::string, Composition>>{
        {"SaaSIntro",          scene_presets::saas_intro()},
        {"FloatingCardsHero",  scene_presets::floating_cards_hero()},
        {"KineticTitle",       scene_presets::kinetic_title()},
        {"DepthProductReveal", scene_presets::depth_product_reveal()},
        {"AppleStyleHero",     scene_presets::apple_style_hero()},
    };

    for (const auto& [name, comp] : presets) {
        CAPTURE(name);
        CHECK(comp.width() == 1920);
        CHECK(comp.height() == 1080);

        // Evaluate at start, middle, and end
        for (Frame f : {Frame{0}, Frame{30}, Frame{60}, Frame{comp.duration() - 1}}) {
            if (f >= Frame{0} && f < comp.duration()) {
                Scene s = evaluate_preset(comp, f);
                CHECK(s.layers().size() > 0);
            }
        }
    }
}
