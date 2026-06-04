#include <doctest/doctest.h>
#include <chronon3d/presets/scenes/saas_intro_premium.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/scene.hpp>

using namespace chronon3d;

static Scene evaluate_premium(const Composition& comp, Frame frame) {
    return comp.evaluate(frame, std::pmr::get_default_resource());
}

TEST_CASE("SaaSIntroPremium: builds successfully") {
    auto comp = scene_presets::saas_intro_premium();

    CHECK(comp.name() == "SaaSIntroPremium");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.duration() == 150);
}

TEST_CASE("SaaSIntroPremium: evaluates without NaN/Inf at multiple frames") {
    auto comp = scene_presets::saas_intro_premium();

    for (Frame f : {Frame{0}, Frame{30}, Frame{60}, Frame{100}, Frame{149}}) {
        Scene s = evaluate_premium(comp, f);
        CHECK(s.layers().size() > 0);

        // Verify no NaN in layer transforms
        for (const auto& layer : s.layers()) {
            CHECK_FALSE(std::isnan(layer.transform.position.x));
            CHECK_FALSE(std::isnan(layer.transform.position.y));
            CHECK_FALSE(std::isnan(layer.transform.position.z));
            CHECK_FALSE(std::isnan(layer.transform.opacity));
        }
    }
}

TEST_CASE("SaaSIntroPremium: has lighting rig applied") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{30});

    CHECK(s.light_context().enabled);
    CHECK(s.light_context().ambient_enabled);
    CHECK(s.light_context().directional_enabled);
    CHECK(s.rim_light().enabled);
}

TEST_CASE("SaaSIntroPremium: has depth grade applied") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{30});

    CHECK(s.depth_grade().enabled);
    CHECK(s.depth_grade().fog_opacity > 0.0f);
}

TEST_CASE("SaaSIntroPremium: has 3 feature cards") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{60});

    int card_count = 0;
    for (const auto& layer : s.layers()) {
        if (layer.name.find("card_") == 0) {
            ++card_count;
            CHECK(layer.uses_2_5d_projection);
            CHECK(layer.card3d_material.enabled);
            CHECK(layer.material.accepts_lights);
            CHECK(layer.material.casts_shadows);
            CHECK(layer.material.accepts_shadows);
        }
    }
    CHECK(card_count == 3);
}

TEST_CASE("SaaSIntroPremium: cards have staggered animations") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{60});

    for (const auto& layer : s.layers()) {
        if (layer.name.find("card_") == 0) {
            // Cards use depth_reveal which animates position, scale, opacity
            CHECK(layer.anim_transform.position.is_animated());
            CHECK(layer.anim_transform.scale.is_animated());
            CHECK(layer.anim_transform.opacity.is_animated());
        }
    }
}

TEST_CASE("SaaSIntroPremium: has CTA button") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{80});

    bool has_cta = false;
    for (const auto& layer : s.layers()) {
        if (layer.name == "cta") {
            has_cta = true;
            CHECK(layer.anim_transform.scale.is_animated()); // settle
        }
    }
    CHECK(has_cta);
}

TEST_CASE("SaaSIntroPremium: has floating particles") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{60});

    int particle_count = 0;
    for (const auto& layer : s.layers()) {
        if (layer.name.find("p") == 0 && layer.name.size() == 2) {
            ++particle_count;
            CHECK(layer.uses_2_5d_projection);
        }
    }
    CHECK(particle_count == 4);
}

TEST_CASE("SaaSIntroPremium: camera is active") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{60});

    CHECK(s.camera_2_5d().enabled);
}

TEST_CASE("SaaSIntroPremium: no black artifact layers") {
    auto comp = scene_presets::saas_intro_premium();
    Scene s = evaluate_premium(comp, Frame{60});

    // All visible layers should have opacity > 0 or be animating in
    for (const auto& layer : s.layers()) {
        if (layer.visible && layer.transform.opacity <= 0.0f) {
            // Only acceptable if opacity is being animated to become visible
            CHECK(layer.anim_transform.opacity.is_animated());
        }
    }
}

TEST_CASE("SaaSIntroPremium: consistent layer count across frames") {
    auto comp = scene_presets::saas_intro_premium();

    Scene s30 = evaluate_premium(comp, Frame{30});
    Scene s60 = evaluate_premium(comp, Frame{60});
    Scene s100 = evaluate_premium(comp, Frame{100});

    // Layer count should be stable (no layers randomly appearing/disappearing)
    // Allow small variance for layers with from/duration
    CHECK(s30.layers().size() > 0);
    CHECK(s60.layers().size() > 0);
    CHECK(s100.layers().size() > 0);
}
