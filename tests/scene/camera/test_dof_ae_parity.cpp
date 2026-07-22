// =============================================================================
// tests/scene/camera/test_dof_ae_parity.cpp
//
// FASE 5 — Depth of Field AE Parity Tests.
//
// Verifies DOF pipeline against After Effects behaviour:
//   • DOF enable/disable toggle             (field propagation)
//   • Focus distance interpolation          (rack focus across frames)
//   • Aperture scale propagation            (legacy simple model)
//   • Physical lens DOF model               (use_physical_model, f_stop, focal_length)
//   • DOF fields survive projection type    (Zoom / FOV / PhysicalLens)
//   • DOF disabled: all fields preserved but no blur processing
//
// Uses the V2 compile_composition → evaluate pipeline (P3-F pattern).
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <chronon3d/core/types/result.hpp>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {
constexpr FrameRate kDofAeFps{60, 1};
constexpr float    kDofEps = 1e-3f;
constexpr int      kW = 320;
constexpr int      kH = 180;

CompositionDefinition make_def(const std::string& name) {
    CompositionDefinition def;
    def.composition.name       = name;
    def.composition.width      = kW;
    def.composition.height     = kH;
    def.composition.frame_rate = kDofAeFps;
    def.composition.duration   = Frame{120};
    def.scene = [](const FrameContext&) { return Scene{}; };

    CameraDescriptor desc;
    desc.id          = name + "_desc";
    desc.source      = StaticCameraSource{};
    desc.orientation = FixedOrientation{};
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    FovProjection proj{};
    proj.fov_deg.set(50.0f);
    desc.base.projection = std::move(proj);
    def.camera = std::move(desc);
    return def;
}

CompositionEvaluateContext make_ctx(Frame fr = Frame{0}) {
    CompositionEvaluateContext ctx;
    ctx.frame_context = frame_context.with_frame_rate(kDofAeFps);
    ctx.frame_context.width      = kW;
    ctx.frame_context.height     = kH;
    ctx.frame_context = frame_context.with_frame(fr);
    return ctx;
}

Camera2_5D eval_one(CompositionDefinition& def, Frame fr = Frame{0}) {
    auto cc_res = compile_composition(def, {});
    REQUIRE(cc_res.has_value());
    auto cc = std::move(cc_res).value();
    REQUIRE(cc.camera_program);
    auto ev_res = evaluate(cc, make_ctx(fr), fr);
    REQUIRE(ev_res.has_value());
    REQUIRE(ev_res->camera.has_value());
    return *ev_res->camera;
}
} // namespace

// ══════════════════════════════════════════════════════════════════════════
// 5.1 — DOF enable/disable toggle
//
// When dof.enabled=false, Camera2_5D.dof.enabled must be false.
// When dof.enabled=true,  Camera2_5D.dof.enabled must be true and all
// DOF fields must carry through unmodified.
// ══════════════════════════════════════════════════════════════════════════
TEST_CASE("FASE 5.1: DOF enable/disable toggle") {
    SUBCASE("DOF disabled by default") {
        CompositionDefinition def = make_def("dof_disabled");
        // Default dof.enabled is false.
        Camera2_5D cam = eval_one(def);
        CHECK_FALSE(cam.dof.enabled);
    }

    SUBCASE("DOF enabled — all fields carry through") {
        CompositionDefinition def = make_def("dof_enabled");
        def.camera->base.dof.enabled        = true;
        def.camera->base.dof.focus_distance = 750.0f;
        def.camera->base.dof.aperture       = 4.0f;
        def.camera->base.dof.max_blur       = 18.0f;

        Camera2_5D cam = eval_one(def);
        CHECK(cam.dof.enabled);
        CHECK(cam.dof.focus_distance == doctest::Approx(750.0f).epsilon(kDofEps));
        CHECK(cam.dof.aperture       == doctest::Approx(4.0f).epsilon(kDofEps));
        CHECK(cam.dof.max_blur       == doctest::Approx(18.0f).epsilon(kDofEps));
    }

    SUBCASE("DOF toggled off → on → off across recompile") {
        CompositionDefinition def = make_def("dof_toggle");

        // Off
        def.camera->base.dof.enabled = false;
        Camera2_5D cam_off = eval_one(def);
        CHECK_FALSE(cam_off.dof.enabled);

        // On
        def.camera->base.dof.enabled = true;
        def.camera->base.dof.focus_distance = 500.0f;
        Camera2_5D cam_on = eval_one(def);
        CHECK(cam_on.dof.enabled);
        CHECK(cam_on.dof.focus_distance == doctest::Approx(500.0f).epsilon(kDofEps));

        // Off again
        def.camera->base.dof.enabled = false;
        Camera2_5D cam_off2 = eval_one(def);
        CHECK_FALSE(cam_off2.dof.enabled);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 5.2 — Animated focus distance (rack focus)
//
// PoseTracksSource with keyframed focus_distance across multiple frames.
// Verifies interpolation between keyframes and that static DOF fields
// (aperture, max_blur) remain constant.
// ══════════════════════════════════════════════════════════════════════════
TEST_CASE("FASE 5.2: Animated focus distance — rack focus") {
    CompositionDefinition def = make_def("dof_rack_focus");

    // Build PoseTracksSource with 3 focus_distance keyframes.
    PoseTracksSource pts;
    pts.focus_distance.key(Frame{0},   200.0f);
    pts.focus_distance.key(Frame{60},  1000.0f);
    pts.focus_distance.key(Frame{120}, 200.0f);
    def.camera->source = pts;

    def.camera->base.dof.enabled  = true;
    def.camera->base.dof.aperture = 0.03f;
    def.camera->base.dof.max_blur = 24.0f;

    SUBCASE("frame 0 — focus at near") {
        Camera2_5D cam = eval_one(def, Frame{0});
        CHECK(cam.dof.enabled);
        CHECK(cam.dof.focus_distance == doctest::Approx(200.0f).epsilon(kDofEps));
        CHECK(cam.dof.aperture       == doctest::Approx(0.03f).epsilon(kDofEps));
        CHECK(cam.dof.max_blur       == doctest::Approx(24.0f).epsilon(kDofEps));
    }

    SUBCASE("frame 60 — focus at far (keyframe peak)") {
        Camera2_5D cam = eval_one(def, Frame{60});
        CHECK(cam.dof.focus_distance == doctest::Approx(1000.0f).epsilon(kDofEps));
    }

    SUBCASE("frame 120 — focus back to near (keyframe end)") {
        Camera2_5D cam = eval_one(def, Frame{120});
        CHECK(cam.dof.focus_distance == doctest::Approx(200.0f).epsilon(kDofEps));
    }

    SUBCASE("frame 30 — midpoint interpolation (200→1000)") {
        Camera2_5D cam = eval_one(def, Frame{30});
        // Linear interpolation: 200 + (1000-200)/2 = 600
        CHECK(cam.dof.focus_distance == doctest::Approx(600.0f).epsilon(kDofEps));
    }

    SUBCASE("frame 90 — midpoint interpolation (1000→200)") {
        Camera2_5D cam = eval_one(def, Frame{90});
        // Linear interpolation: 1000 + (200-1000)/2 = 600
        CHECK(cam.dof.focus_distance == doctest::Approx(600.0f).epsilon(kDofEps));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 5.3 — Aperture scale propagation
//
// The legacy simple DOF model uses aperture as blur-per-unit-Z-distance.
// Different aperture values must carry through the compile→evaluate pipeline.
// ══════════════════════════════════════════════════════════════════════════
TEST_CASE("FASE 5.3: Aperture scale propagation") {
    CompositionDefinition def = make_def("dof_aperture");
    def.camera->base.dof.enabled  = true;
    def.camera->base.dof.max_blur = 24.0f;

    const std::vector<float> apertures = {0.01f, 0.03f, 0.1f, 1.0f, 4.0f};
    for (float ap : apertures) {
        def.camera->base.dof.aperture = ap;
        Camera2_5D cam = eval_one(def);
        INFO("aperture=", ap);
        CHECK(cam.dof.aperture == doctest::Approx(ap).epsilon(kDofEps));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 5.4 — Physical lens DOF model
//
// When use_physical_model=true, the DOF uses focal_length, f_stop, and
// sensor dimensions instead of the legacy aperture/max_blur model.
// All physical lens fields must carry through the pipeline.
// ══════════════════════════════════════════════════════════════════════════
TEST_CASE("FASE 5.4: Physical lens DOF model") {
    CompositionDefinition def = make_def("dof_physical_lens");
    def.camera->base.dof.enabled            = true;
    def.camera->base.dof.use_physical_model = true;
    def.camera->base.dof.focus_distance     = 800.0f;

    // Use a wide-angle cinema lens preset.
    def.camera->base.lens.focal_length  = 24.0f;
    def.camera->base.lens.f_stop        = 2.0f;
    def.camera->base.lens.sensor_width  = 36.0f;
    def.camera->base.lens.sensor_height = 24.0f;
    def.camera->base.lens.gate_fit      = GateFit::Fill;

    SUBCASE("physical lens fields propagate to Camera2_5D") {
        Camera2_5D cam = eval_one(def);
        CHECK(cam.dof.enabled);
        CHECK(cam.dof.use_physical_model);
        CHECK(cam.dof.focus_distance     == doctest::Approx(800.0f).epsilon(kDofEps));
        CHECK(cam.lens.focal_length      == doctest::Approx(24.0f).epsilon(kDofEps));
        CHECK(cam.lens.f_stop            == doctest::Approx(2.0f).epsilon(kDofEps));
        CHECK(cam.lens.sensor_width      == doctest::Approx(36.0f).epsilon(kDofEps));
        CHECK(cam.lens.sensor_height     == doctest::Approx(24.0f).epsilon(kDofEps));
    }

    SUBCASE("physical lens model toggle preserves DOF fields") {
        // With physical model off, legacy fields should still work.
        def.camera->base.dof.use_physical_model = false;
        def.camera->base.dof.aperture           = 0.015f;
        def.camera->base.dof.max_blur           = 18.0f;

        Camera2_5D cam = eval_one(def);
        CHECK_FALSE(cam.dof.use_physical_model);
        CHECK(cam.dof.aperture == doctest::Approx(0.015f).epsilon(kDofEps));
        CHECK(cam.dof.max_blur == doctest::Approx(18.0f).epsilon(kDofEps));
        // focus_distance is still valid in legacy mode.
        CHECK(cam.dof.focus_distance == doctest::Approx(800.0f).epsilon(kDofEps));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 5.5 — DOF fields survive projection type changes
//
// DOF settings must be independent of the projection mode (Zoom, FOV,
// PhysicalLens).  Switching between projection types must not affect
// the DOF fields in the evaluated camera.
// ══════════════════════════════════════════════════════════════════════════
TEST_CASE("FASE 5.5: DOF survives projection type changes") {
    CompositionDefinition def = make_def("dof_projection");
    def.camera->base.dof.enabled        = true;
    def.camera->base.dof.focus_distance = 600.0f;
    def.camera->base.dof.aperture       = 2.0f;
    def.camera->base.dof.max_blur       = 12.0f;

    SUBCASE("Zoom projection — DOF intact") {
        ZoomProjection zp{};
        zp.zoom.set(1200.0f);
        def.camera->base.projection = std::move(zp);

        Camera2_5D cam = eval_one(def);
        CHECK(cam.dof.enabled);
        CHECK(cam.zoom == doctest::Approx(1200.0f).epsilon(kDofEps));
        CHECK(cam.dof.focus_distance == doctest::Approx(600.0f).epsilon(kDofEps));
        CHECK(cam.dof.aperture       == doctest::Approx(2.0f).epsilon(kDofEps));
    }

    SUBCASE("FOV projection — DOF intact") {
        FovProjection fp{};
        fp.fov_deg.set(65.0f);
        def.camera->base.projection = std::move(fp);

        Camera2_5D cam = eval_one(def);
        CHECK(cam.dof.enabled);
        CHECK(cam.fov_deg == doctest::Approx(65.0f).epsilon(kDofEps));
        CHECK(cam.dof.focus_distance == doctest::Approx(600.0f).epsilon(kDofEps));
        CHECK(cam.dof.aperture       == doctest::Approx(2.0f).epsilon(kDofEps));
    }

    SUBCASE("PhysicalLens projection — DOF intact") {
        PhysicalLensProjection plp{};
        plp.lens.focal_length  = 35.0f;
        plp.lens.f_stop        = 1.4f;
        plp.lens.sensor_width  = 36.0f;
        plp.lens.sensor_height = 24.0f;
        plp.lens.gate_fit      = GateFit::Fill;
        def.camera->base.projection = std::move(plp);

        Camera2_5D cam = eval_one(def);
        CHECK(cam.dof.enabled);
        CHECK(cam.lens.focal_length == doctest::Approx(35.0f).epsilon(kDofEps));
        CHECK(cam.dof.focus_distance == doctest::Approx(600.0f).epsilon(kDofEps));
        CHECK(cam.dof.aperture       == doctest::Approx(2.0f).epsilon(kDofEps));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 5.6 — DOF disabled: field preservation
//
// When DOF is disabled, all DOF fields must still be preserved in the
// evaluated camera (they just won't trigger blur processing).  A disabled
// DOF with specific field values must still report those values.
// ══════════════════════════════════════════════════════════════════════════
TEST_CASE("FASE 5.6: DOF disabled preserves field values") {
    CompositionDefinition def = make_def("dof_disabled_fields");
    def.camera->base.dof.enabled        = false;
    def.camera->base.dof.focus_distance = 400.0f;
    def.camera->base.dof.aperture       = 1.5f;
    def.camera->base.dof.max_blur       = 16.0f;

    Camera2_5D cam = eval_one(def);
    CHECK_FALSE(cam.dof.enabled);
    // Fields are preserved even when DOF is disabled.
    CHECK(cam.dof.focus_distance == doctest::Approx(400.0f).epsilon(kDofEps));
    CHECK(cam.dof.aperture       == doctest::Approx(1.5f).epsilon(kDofEps));
    CHECK(cam.dof.max_blur       == doctest::Approx(16.0f).epsilon(kDofEps));
}
