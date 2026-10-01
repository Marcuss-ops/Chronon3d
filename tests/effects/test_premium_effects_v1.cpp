#include <doctest/doctest.h>

#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/graphics/stroke/brush_profile.hpp>
#include <chronon3d/graphics/distance_field2d.hpp>
#include <chronon3d/graphics/vector_field2d.hpp>
#include <chronon3d/graphics/gradient_mesh.hpp>
#include <chronon3d/effects/motion_field.hpp>
#include <chronon3d/effects/temporal_history.hpp>
#include <chronon3d/effects/backdrop_filter.hpp>
#include <chronon3d/effects/luminance_response.hpp>
#include <chronon3d/render_plan/effect_presets.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include "processors/effects/distort/warp.hpp"
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

using namespace chronon3d;
using namespace chronon3d::graphics;
using namespace chronon3d::effects;
using namespace chronon3d::render_plan;
using namespace chronon3d::renderer;

TEST_CASE("Premium Effects V1: Field2D transform and combined operators") {
    SUBCASE("FieldTransform2D applies translation, scale, and rotation") {
        FieldTransform2D xform;
        xform.translation = Vec2{0.1f, -0.2f};
        xform.scale = Vec2{2.0f, 0.5f};
        xform.rotation_radians = 3.14159265f * 0.5f; // 90 degrees
        xform.pivot = Vec2{0.5f, 0.5f};

        const Vec2 p{0.5f, 0.5f};
        const Vec2 transformed = xform.apply(p);
        CHECK(doctest::Approx(transformed.x).epsilon(1e-4) == 0.6f);
        CHECK(doctest::Approx(transformed.y).epsilon(1e-4) == 0.3f);
    }

    SUBCASE("Field2D grid transform produces isometric-style coordinates without new generator") {
        Field2D iso_grid;
        iso_grid.generator = Field2DGenerator::Grid;
        iso_grid.frequency = 8.0f;
        iso_grid.transform.rotation_radians = 0.523598775f; // 30 degrees
        iso_grid.transform.scale = Vec2{1.0f, 0.5f};        // isometric foreshortening

        const float sample1 = iso_grid.sample(Vec2{0.5f, 0.5f});
        const float sample2 = iso_grid.sample(Vec2{0.6f, 0.55f});
        CHECK(sample1 >= 0.0f);
        CHECK(sample1 <= 1.0f);
        CHECK(sample2 >= 0.0f);
        CHECK(sample2 <= 1.0f);
    }

    SUBCASE("Field2D combined operators: Add, Multiply, Clamp, Power") {
        Field2D field;
        field.generator = Field2DGenerator::Linear; // x coordinate [0, 1]
        field.frequency = 1.0f;

        Field2DOperator op_add;
        op_add.kind = Field2DOperatorKind::Add;
        op_add.scalar = 0.2f;

        Field2DOperator op_mul;
        op_mul.kind = Field2DOperatorKind::Multiply;
        op_mul.scalar = 2.0f;

        Field2DOperator op_clamp;
        op_clamp.kind = Field2DOperatorKind::Clamp;
        op_clamp.scalar = 0.0f;
        op_clamp.scalar_b = 0.9f;

        field.operators = {op_add, op_mul, op_clamp};

        // Linear at x=0.1 -> base=0.1 -> +0.2 = 0.3 -> *2 = 0.6 -> clamped = 0.6
        const float val = field.sample(Vec2{0.1f, 0.5f});
        CHECK(doctest::Approx(val).epsilon(1e-3) == 0.6f);
    }

    SUBCASE("Field2D fingerprint determinism") {
        Field2D f1;
        f1.seed = 12345;
        f1.frequency = 6.0f;
        f1.transform.scale = Vec2{1.5f, 1.5f};

        Field2D f2 = f1;
        CHECK(field2d_fingerprint(f1) == field2d_fingerprint(f2));

        f2.transform.rotation_radians = 0.4f;
        CHECK(field2d_fingerprint(f1) != field2d_fingerprint(f2));
    }
}

TEST_CASE("Premium Effects V1: BrushProfile primitive authority") {
    SUBCASE("Highlighter preset determinism and multiply blend suitability") {
        const auto hl1 = BrushProfile::highlighter(42);
        const auto hl2 = BrushProfile::highlighter(42);
        const auto hl_diff_seed = BrushProfile::highlighter(999);

        // Core center alpha
        const float a1 = hl1.sample_alpha(0.0f, 0.5f, Vec2{100.0f, 50.0f});
        const float a2 = hl2.sample_alpha(0.0f, 0.5f, Vec2{100.0f, 50.0f});
        CHECK(a1 == a2);
        CHECK(a1 > 0.0f);
        CHECK(a1 <= 1.0f);

        // Edge accumulation: highlighter edge density accumulates pigment at stroke borders
        const float mid = hl1.sample_alpha(0.3f, 0.5f, Vec2{100.0f, 50.0f});
        const float edge = hl1.sample_alpha(0.8f, 0.5f, Vec2{100.0f, 50.0f});
        CHECK(edge > 0.0f);
        CHECK(mid > 0.0f);

        // Outside nominal width falls off to 0
        const float outside = hl1.sample_alpha(2.0f, 0.5f, Vec2{100.0f, 50.0f});
        CHECK(outside == 0.0f);
    }

    SUBCASE("Chalk / Scribble presets have higher grain and jitter") {
        const auto chalk = BrushProfile::pencil_chalk(777);
        CHECK(chalk.grain_strength > 0.3f);
        CHECK(chalk.edge_softness >= 0.25f);

        const auto scribble = BrushProfile::scribble(1337);
        CHECK(scribble.width_jitter > 0.15f);
    }
}

TEST_CASE("Premium Effects V1: DistanceField2D math and edge operators") {
    SUBCASE("SDF primitives") {
        const float d_circ = DistanceField2D::circle(Vec2{5.0f, 0.0f}, Vec2{0.0f, 0.0f}, 3.0f);
        CHECK(doctest::Approx(d_circ).epsilon(1e-4) == 2.0f); // 5 - 3 = 2 outside

        const float d_box = DistanceField2D::box(Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f}, Vec2{4.0f, 4.0f});
        CHECK(d_box < 0.0f); // negative inside
    }

    SUBCASE("Smooth CSG union") {
        const float d1 = 1.0f;
        const float d2 = 1.2f;
        const float u_sharp = DistanceField2D::union_op(d1, d2);
        const float u_smooth = DistanceField2D::smooth_union(d1, d2, 0.5f);
        CHECK(u_sharp == 1.0f);
        CHECK(u_smooth < u_sharp); // smooth union rounds/blends boundaries
    }

    SUBCASE("Outline and Rim shading") {
        const float outline = DistanceField2D::outline(0.0f, 4.0f, 1.0f);
        CHECK(outline == 1.0f); // maximum on border

        const float rim_inside = DistanceField2D::rim(-1.0f, 3.0f, 0.5f);
        const float rim_outside = DistanceField2D::rim(1.0f, 3.0f, 0.5f);
        CHECK(rim_inside > 0.0f);
        CHECK(rim_outside == 0.0f); // rim is strictly inside shape
    }
}

TEST_CASE("Premium Effects V1: VectorField2D and GradientMesh") {
    SUBCASE("VectorField2D heatwave drift") {
        VectorField2D heat;
        heat.type = VectorFieldType::HeatwaveVertical;
        heat.strength = 0.08f;
        heat.speed = 2.0f;

        const Vec2 v0 = heat.sample(Vec2{0.5f, 0.5f}, 0.0f);
        const Vec2 v1 = heat.sample(Vec2{0.5f, 0.5f}, 1.0f);
        CHECK(std::isfinite(v0.x));
        CHECK(std::isfinite(v0.y));
        CHECK(std::isfinite(v1.x));
        CHECK(std::isfinite(v1.y));
        CHECK(glm::length(v0) <= 0.2f);
    }

    SUBCASE("GradientMesh multi-node interpolation") {
        GradientMesh mesh = GradientMesh::four_corner(
            Color{1, 0, 0, 1}, Color{0, 1, 0, 1},
            Color{0, 0, 1, 1}, Color{1, 1, 0, 1});

        const Color c_tl = mesh.sample(Vec2{0.0f, 0.0f});
        CHECK(c_tl.r == 1.0f);
        CHECK(c_tl.g == 0.0f);
        CHECK(c_tl.b == 0.0f);

        const Color c_center = mesh.sample(Vec2{0.5f, 0.5f});
        CHECK(c_center.r > 0.0f);
        CHECK(c_center.g > 0.0f);
        CHECK(c_center.b > 0.0f);
        CHECK(doctest::Approx(c_center.a) == 1.0f);
    }
}

TEST_CASE("Premium Effects V1: MotionField & Velocity RGB Split") {
    MotionField field;
    field.velocity = Vec2{15.0f, 0.0f}; // moving horizontally right

    const Vec2 vel = field.sample(Vec2{0.5f, 0.5f});
    CHECK(vel.x == 15.0f);
    CHECK(vel.y == 0.0f);

    LayerPlan layer;
    effect_presets::velocity_rgb_split(layer, vel, 6.0f);
    REQUIRE(layer.effects.size() == 1);
    CHECK(layer.effects[0].kind == LayerPlan::EffectKindPlan::ChromaticAberration);
    CHECK(layer.effects[0].chromatic_offset_mode == 2);
    CHECK(layer.effects[0].chromatic_velocity[0] == 15.0f);
    CHECK(layer.effects[0].chromatic_red_offset == 6.0f);
}

TEST_CASE("Premium Effects V1: TemporalHistory determinism") {
    TemporalHistoryConfig hist;
    hist.required_history_frames = 6;
    hist.persistence_decay = 0.8f;

    CHECK(hist.validate());
    CHECK(doctest::Approx(hist.weight_at_offset(0)).epsilon(1e-4) == 1.0f);
    CHECK(doctest::Approx(hist.weight_at_offset(1)).epsilon(1e-4) == 0.8f);
    CHECK(doctest::Approx(hist.weight_at_offset(2)).epsilon(1e-4) == 0.64f);
    CHECK(hist.weight_at_offset(10) == 0.0f); // beyond required history window
}

TEST_CASE("Premium Effects V1: BackdropFilter authority fail-closed validation") {
    BackdropFilterPlan valid = BackdropFilterPlan::glass_frosted();
    CHECK_NOTHROW(valid.validate());

    BackdropFilterPlan invalid = valid;
    invalid.blur = 9999.0f; // exceeds 256
    CHECK_THROWS_AS(invalid.validate(), std::invalid_argument);

    invalid = valid;
    invalid.opacity = -0.5f;
    CHECK_THROWS_AS(invalid.validate(), std::invalid_argument);
}

TEST_CASE("Premium Effects V1: Execution on Framebuffer (LensDistortion, FoldWarp, CreaseShading, LuminanceResponse)") {
    Framebuffer fb(64, 64);
    fb.clear(Color{0.5f, 0.5f, 0.5f, 1.0f});
    EffectScratchResources scratch;

    SUBCASE("LensDistortion applies without NaN") {
        LensDistortionParams p;
        p.curvature = 0.2f;
        p.scale = 1.05f;
        apply_lens_distortion(fb, p, scratch);

        for (int y = 0; y < fb.height(); ++y) {
            for (int x = 0; x < fb.width(); ++x) {
                const Color c = fb.get_pixel(x, y);
                CHECK(std::isfinite(c.r));
                CHECK(std::isfinite(c.g));
                CHECK(std::isfinite(c.b));
                CHECK(std::isfinite(c.a));
            }
        }
    }

    SUBCASE("FoldWarp with amount=0 is identity") {
        Framebuffer copy = fb;
        FoldWarpParams p;
        p.amount = 0.0f;
        apply_fold_warp(fb, p, scratch);

        for (int y = 0; y < fb.height(); ++y) {
            for (int x = 0; x < fb.width(); ++x) {
                const Color c1 = fb.get_pixel(x, y);
                const Color c2 = copy.get_pixel(x, y);
                CHECK(doctest::Approx(c1.r).epsilon(1e-3) == c2.r);
            }
        }
    }

    SUBCASE("CreaseShading modulates shadow and highlight across crease") {
        CreaseShadingParams p;
        p.axis = FoldAxis::Vertical;
        p.position = 0.5f;
        p.width = 16.0f;
        p.dark_side = 0.40f;
        p.light_side = 0.25f;
        apply_crease_shading(fb, p);

        // Center-left should be darkened (x=30)
        const Color c_dark = fb.get_pixel(30, 32);
        // Center-right should be highlighted (x=34)
        const Color c_light = fb.get_pixel(34, 32);
        CHECK(c_dark.r < 0.5f);
        CHECK(c_light.r > 0.5f);
    }

    SUBCASE("LuminanceResponse triggers on bright highlights") {
        fb.clear(Color{0.1f, 0.1f, 0.1f, 1.0f});
        // Paint a bright square in the center
        for (int y = 28; y <= 36; ++y) {
            for (int x = 28; x <= 36; ++x) {
                fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
            }
        }

        LuminanceResponseParams p = LuminanceResponseParams::halation();
        p.radius = 8.0f;
        apply_luminance_response(fb, p, scratch);

        // Pixel just outside square (e.g. 25, 32) should receive warm halation bleed
        const Color c_bleed = fb.get_pixel(25, 32);
        CHECK(c_bleed.r > 0.1f);
        CHECK(c_bleed.r > c_bleed.b); // warm red/orange tint has r > b
    }
}

TEST_CASE("Premium Effects V1: Presets and Recipes composition") {
    LayerPlan layer;

    SUBCASE("CRT recipe populates canonical primitives stack") {
        effect_presets::crt(layer);
        REQUIRE(layer.effects.size() >= 4);
        CHECK(layer.effects[0].kind == LayerPlan::EffectKindPlan::LensDistortion);
        CHECK(layer.effects[1].kind == LayerPlan::EffectKindPlan::WaveWarp); // scanlines
        CHECK(layer.effects[2].kind == LayerPlan::EffectKindPlan::ChromaticAberration);
        CHECK(layer.effects[3].kind == LayerPlan::EffectKindPlan::Bloom);
    }

    SUBCASE("Paper fold recipe populates FoldWarp + CreaseShading") {
        LayerPlan fold_layer;
        effect_presets::paper_fold(fold_layer, 0.35f, 0.5f);
        REQUIRE(fold_layer.effects.size() == 2);
        CHECK(fold_layer.effects[0].kind == LayerPlan::EffectKindPlan::FoldWarp);
        CHECK(fold_layer.effects[1].kind == LayerPlan::EffectKindPlan::CreaseShading);
    }

    SUBCASE("Anamorphic flare recipe populates LuminanceResponse + DirectionalBlur") {
        LayerPlan flare_layer;
        effect_presets::anamorphic_flare(flare_layer, 64.0f);
        REQUIRE(flare_layer.effects.size() == 2);
        CHECK(flare_layer.effects[0].kind == LayerPlan::EffectKindPlan::LuminanceResponse);
        CHECK(flare_layer.effects[1].kind == LayerPlan::EffectKindPlan::DirectionalBlur);
    }

    SUBCASE("Frosted glass recipe configures BackdropFilterPlan") {
        LayerPlan glass_layer;
        effect_presets::frosted_glass(glass_layer);
        REQUIRE(glass_layer.backdrop_filter.has_value());
        CHECK(glass_layer.backdrop_filter->blur == 28.0f);
        CHECK(glass_layer.backdrop_filter->rim_strength > 0.0f);
    }
}
