#include <doctest/doctest.h>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <chronon3d/scene/model/material_2_5d.hpp>
#include <glm/glm.hpp>

using namespace chronon3d;
using namespace chronon3d::rendering;

TEST_CASE("LightingRig: RimLight defaults") {
    RimLight rim;
    CHECK_FALSE(rim.enabled);
    CHECK(rim.color.r == doctest::Approx(1.0f));
    CHECK(rim.color.g == doctest::Approx(1.0f));
    CHECK(rim.color.b == doctest::Approx(1.0f));
    CHECK(rim.intensity == doctest::Approx(0.30f));
    CHECK(rim.power == doctest::Approx(2.2f));
}

TEST_CASE("LightingRig: SaaSBlue preset validates") {
    auto rig = LightingRig::SaaSBlue();
    CHECK(rig.key_intensity == doctest::Approx(0.85f));
    CHECK(rig.rim.enabled);
    CHECK(rig.rim.color.r == doctest::Approx(0.60f));
    CHECK(rig.rim.color.g == doctest::Approx(0.75f));
    CHECK(rig.rim.color.b == doctest::Approx(1.0f));
    CHECK(rig.rim.power == doctest::Approx(2.5f));
    CHECK(rig.exposure == doctest::Approx(1.0f));
    // Direction should be normalized
    float len = glm::length(rig.key_direction);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("LightingRig: PurpleNeon preset validates") {
    auto rig = LightingRig::PurpleNeon();
    CHECK(rig.key_intensity == doctest::Approx(0.90f));
    CHECK(rig.rim.enabled);
    CHECK(rig.rim.color.r == doctest::Approx(0.40f));
    CHECK(rig.rim.color.g == doctest::Approx(0.90f));
    CHECK(rig.rim.color.b == doctest::Approx(1.0f));
    CHECK(rig.exposure == doctest::Approx(1.10f));
    float len = glm::length(rig.key_direction);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("LightingRig: AppleDark preset validates") {
    auto rig = LightingRig::AppleDark();
    CHECK(rig.key_intensity == doctest::Approx(0.70f));
    CHECK(rig.rim.enabled);
    // Apple Dark has subdued rim
    CHECK(rig.rim.intensity == doctest::Approx(0.25f));
    CHECK(rig.rim.power == doctest::Approx(3.0f));
    // Higher shadow opacity for moody look
    CHECK(rig.shadows.opacity == doctest::Approx(0.40f));
    CHECK(rig.shadows.ambient_blur_radius == doctest::Approx(100.0f));
    float len = glm::length(rig.key_direction);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("LightingRig: CleanWhite preset validates") {
    auto rig = LightingRig::CleanWhite();
    CHECK(rig.key_intensity == doctest::Approx(0.78f));
    CHECK(rig.rim.enabled);
    // White ambient
    CHECK(rig.ambient_color.r == doctest::Approx(0.85f));
    CHECK(rig.ambient_color.g == doctest::Approx(0.87f));
    CHECK(rig.ambient_color.b == doctest::Approx(0.90f));
    CHECK(rig.ambient_intensity == doctest::Approx(0.30f));
    // Lower shadow for clean look
    CHECK(rig.shadows.opacity == doctest::Approx(0.25f));
    float len = glm::length(rig.key_direction);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("LightingRig: apply_to configures LightContext") {
    auto rig = LightingRig::SaaSBlue();
    LightContext ctx;
    RimLight rim;
    ctx.enabled = false;
    ctx.ambient_enabled = false;
    ctx.directional_enabled = false;

    rig.apply_to(ctx, &rim);

    CHECK(ctx.enabled);
    CHECK(ctx.ambient_enabled);
    CHECK(ctx.directional_enabled);
    CHECK(ctx.direction.x == doctest::Approx(rig.key_direction.x));
    CHECK(ctx.directional_color.r == doctest::Approx(rig.key_color.r));
    CHECK(ctx.diffuse == doctest::Approx(rig.key_intensity));
    CHECK(ctx.ambient == doctest::Approx(rig.ambient_intensity));
    CHECK(ctx.shadows.opacity == doctest::Approx(rig.shadows.opacity));

    // Rim should be populated
    CHECK(rim.enabled);
    CHECK(rim.color.r == doctest::Approx(0.60f));
}

TEST_CASE("LightingRig: apply_to without rim_out") {
    auto rig = LightingRig::CleanWhite();
    LightContext ctx;
    rig.apply_to(ctx); // no rim_out — should not crash
    CHECK(ctx.enabled);
    CHECK(ctx.diffuse == doctest::Approx(0.78f));
}

TEST_CASE("LightingRig: RimLight affects evaluate_lighting") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    mat.specular = 0.0f; // zero specular — rim comes from RimLight only

    LightContext light = LightContext::default_scene();
    Vec3 normal{0.0f, 0.0f, 1.0f}; // facing viewer

    // Without RimLight: no rim contribution since specular=0
    Color no_rim = evaluate_lighting(Color{1,1,1,1}, normal, light, mat, nullptr);

    // With RimLight: should have extra rim contribution on edges
    RimLight rim;
    rim.enabled = true;
    rim.color = {1.0f, 0.5f, 0.0f, 1.0f}; // orange rim
    rim.intensity = 1.0f;
    rim.power = 1.0f;

    // Normal pointing slightly away from viewer → strong rim
    Vec3 edge_normal = glm::normalize(Vec3{0.5f, 0.0f, 0.8f});
    Color with_rim = evaluate_lighting(Color{1,1,1,1}, edge_normal, light, mat, &rim);

    // Rim should add non-zero contribution to red channel
    CHECK(with_rim.r > no_rim.r);
    // With orange rim, red should be stronger than blue
    CHECK(with_rim.r > with_rim.b);
}

TEST_CASE("LightingRig: evaluate_lighting with null rim override uses specular rim") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    mat.specular = 0.5f; // has specular → legacy rim should work

    LightContext light = LightContext::default_scene();
    Vec3 edge_normal = glm::normalize(Vec3{0.5f, 0.0f, 0.8f});

    // Without rim override (nullptr) → should still use specular-derived rim
    Color result = evaluate_lighting(Color{1,1,1,1}, edge_normal, light, mat, nullptr);

    // Face-down normal should get specular rim contribution
    CHECK(result.r > 0.0f);
    CHECK(result.g > 0.0f);
    CHECK(result.b > 0.0f);
}
