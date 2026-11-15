#include <doctest/doctest.h>

#include <chronon3d/rendering/ae_high_quality_style.hpp>
#include <chronon3d/rendering/image_lighting.hpp>

#include <cmath>

using namespace chronon3d;
using namespace chronon3d::rendering;
using namespace chronon3d::rendering::ae_style;

TEST_CASE("AeHighQualityStyle: 2.5D gel refraction and 3D-like bevel text presets") {
    const TextMaterial gel = gel_refraction_25d();
    CHECK(gel.enabled);
    CHECK(gel.gel_surface);
    CHECK(gel.gel_transmission_enabled);
    CHECK(gel.gel_soft_fusion);
    CHECK(gel.bevel_px == doctest::Approx(3.0f));

    const TextMaterial ext = extruded_3d_bevel(18.0f, 4.5f, 2.5f);
    CHECK(ext.enabled);
    CHECK(ext.extruded_text);
    CHECK(ext.extrusion_depth == doctest::Approx(18.0f));
    CHECK(ext.extrusion_bevel == doctest::Approx(4.5f));
    CHECK(ext.bevel_px == doctest::Approx(2.5f));

    const TextMaterial card = beveled_motion_3d_like();
    CHECK(card.enabled);
    CHECK(card.extruded_text);
    CHECK(card.extrusion_depth == doctest::Approx(12.0f));
    CHECK(card.extrusion_bevel == doctest::Approx(4.0f));
    CHECK(card.bevel_px == doctest::Approx(4.0f));
}

TEST_CASE("AeHighQualityStyle: all 10 bust lighting presets sweep light between t=0 and t=1") {
    const auto presets = all_bust_lighting_presets();
    CHECK(presets.size() == 10);

    ImageLightingSample sample{};
    sample.albedo = {0.72f, 0.70f, 0.66f, 1.0f};
    sample.normal_world = safe_normalize(Vec3{-0.45f, -0.18f, 0.87f});
    sample.pixel_world = {820.0f, 500.0f, 0.0f};
    sample.coverage = 1.0f;
    sample.edge_mask = 0.6f;
    sample.edge_outward = {-1.0f, 0.0f};

    for (const BustLightingPreset preset : presets) {
        CAPTURE(to_string(preset));
        const Material2_5D mat = make_bust_material(preset);
        CHECK(mat.accepts_lights);
        CHECK(mat.image_bump_strength > 0.5f);

        const ImageLightingParams params = make_bust_image_params(preset);
        const LightingRig rig = make_bust_lighting_rig(preset);

        const LightContext ctx_start = build_bust_light_context(preset, 0.0f);
        const LightContext ctx_mid = build_bust_light_context(preset, 0.5f);
        const LightContext ctx_end = build_bust_light_context(preset, 1.0f);

        CHECK_FALSE(ctx_start.point_lights.empty());
        CHECK_FALSE(ctx_mid.point_lights.empty());
        CHECK_FALSE(ctx_end.point_lights.empty());

        const Color c0 = evaluate_image_lighting(sample, ctx_start, params, &rig.rim);
        const Color c1 = evaluate_image_lighting(sample, ctx_mid, params, &rig.rim);
        const Color c2 = evaluate_image_lighting(sample, ctx_end, params, &rig.rim);

        const f32 delta_01 = std::abs(c0.r - c1.r) + std::abs(c0.g - c1.g) + std::abs(c0.b - c1.b);
        const f32 delta_12 = std::abs(c1.r - c2.r) + std::abs(c1.g - c2.g) + std::abs(c1.b - c2.b);
        CHECK((delta_01 + delta_12) > 0.02f);
    }
}
