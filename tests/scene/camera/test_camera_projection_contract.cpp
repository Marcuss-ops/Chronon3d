// ============================================================================
// test_camera_projection_contract.cpp
//
// Unified Camera Projection Contract tests — migrated to current API (June 2026).
//
// Verifies that all 4 projection paths produce consistent results:
//   1. camera_math::project_world_point  (contract)
//   2. project_world_to_screen            (Path 1)
//   3. project_layer_2_5d                 (Path 2)
//   4. renderer::make_projection_context  (Path 3)
// ============================================================================
// Enable SUPER_FAST_ASSERTS for this TU if the project build system has not
// already set it (avoids "macro redefined" warnings when the flag is also
// passed via -D on the compiler command line).
#ifndef DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#endif
#include <doctest/doctest.h>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/shape/transform_3d.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp>        // CAM-03 parity test
#include <chronon3d/scene/camera/camera_v1/evaluated_projection.hpp>          // CAM-03 snapshot
#include <cmath>
using namespace chronon3d;
using namespace chronon3d::camera_v1;

// ── Migration helper ───────────────────────────────────────────────────────
//
// CameraRig et al. consume a `const ResolvedSceneTransforms&`.  Build a
// named Transform3D list via `resolve_scene_transforms()` (the canonical
// service) for each pair of test cases that previously used SceneTransformRegistry.

namespace {
ResolvedSceneTransforms build_projection_resolver(
    std::vector<std::pair<std::string, Transform3D>> entries
) {
    std::vector<SceneTransformInput> inputs;
    inputs.reserve(entries.size());
    for (auto& e : entries) {
        inputs.push_back(SceneTransformInput{std::move(e.first), std::move(e.second), false, false});
    }
    return resolve_scene_transforms(inputs);
}
} // anonymous namespace

TEST_CASE("Camera projection contract: center point projects to viewport centre") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto p = camera_math::project_world_point(cam, Vec3{0, 0, 0}, camera_math::Viewport2D{1920, 1080});
    CHECK(p.visible);
    CHECK(p.screen.x == doctest::Approx(960.0f));
    CHECK(p.screen.y == doctest::Approx(540.0f));
    CHECK(p.depth == doctest::Approx(1000.0f));
    CHECK(p.perspective_scale == doctest::Approx(1.0f));
}

TEST_CASE("Camera projection contract: Y sign is inverted (screen Y down)") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto above = camera_math::project_world_point(cam, Vec3{0, 100, 0}, camera_math::Viewport2D{1920, 1080});
    auto center = camera_math::project_world_point(cam, Vec3{0, 0, 0}, camera_math::Viewport2D{1920, 1080});
    CHECK(above.visible);
    CHECK(center.visible);
    CHECK(above.screen.y < center.screen.y);
    auto below = camera_math::project_world_point(cam, Vec3{0, -100, 0}, camera_math::Viewport2D{1920, 1080});
    CHECK(below.screen.y > center.screen.y);
}

TEST_CASE("Camera projection contract: X sign is correct") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto right = camera_math::project_world_point(cam, Vec3{100, 0, 0}, camera_math::Viewport2D{1920, 1080});
    auto center = camera_math::project_world_point(cam, Vec3{0, 0, 0}, camera_math::Viewport2D{1920, 1080});
    CHECK(right.visible);
    CHECK(right.screen.x > center.screen.x);
}

TEST_CASE("Camera projection contract: points behind camera are invisible") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto p = camera_math::project_world_point(cam, Vec3{0, 0, -1200}, camera_math::Viewport2D{1920, 1080});
    CHECK_FALSE(p.visible);
}

TEST_CASE("Camera projection contract: depth increases with distance") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto near_pt = camera_math::project_world_point(cam, Vec3{0, 0, -500}, camera_math::Viewport2D{1920, 1080});
    auto far_pt = camera_math::project_world_point(cam, Vec3{0, 0, 500}, camera_math::Viewport2D{1920, 1080});
    CHECK(near_pt.visible);
    CHECK(far_pt.visible);
    CHECK(near_pt.depth == doctest::Approx(500.0f).epsilon(1.0f));
    CHECK(far_pt.depth == doctest::Approx(1500.0f).epsilon(1.0f));
}

TEST_CASE("Camera projection contract: perspective scale varies with depth") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto near_pt = camera_math::project_world_point(cam, Vec3{0, 0, -500}, camera_math::Viewport2D{1920, 1080});
    auto far_pt = camera_math::project_world_point(cam, Vec3{0, 0, 500}, camera_math::Viewport2D{1920, 1080});
    CHECK(near_pt.perspective_scale == doctest::Approx(2.0f).epsilon(0.01f));
    CHECK(far_pt.perspective_scale == doctest::Approx(0.6667f).epsilon(0.01f));
}

TEST_CASE("Camera projection contract: FOV and zoom focal equivalence") {
    Camera2_5D cam_fov;
    cam_fov.enabled = true;
    cam_fov.position = {0, 0, -1000};
    cam_fov.fov_deg = 90.0f;
    const f32 focal = camera_math::focal_from_camera(cam_fov, 1920, 1080);
    Camera2_5D    cam_zoom = cam_fov;
    cam_zoom.zoom = focal;
    auto a = camera_math::project_world_point(cam_fov, {100, 0, 0}, {1920, 1080});
    auto b = camera_math::project_world_point(cam_zoom, {100, 0, 0}, {1920, 1080});
    CHECK(a.screen.x == doctest::Approx(b.screen.x).epsilon(0.0001f));
    CHECK(a.screen.y == doctest::Approx(b.screen.y).epsilon(0.0001f));
    CHECK(a.depth == doctest::Approx(b.depth).epsilon(0.0001f));
}

TEST_CASE("TICKET-035: invisible points have safe defaults, NOT numeric sentinels") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto p = camera_math::project_world_point(cam, Vec3{0, 0, -1001}, camera_math::Viewport2D{1920, 1080});
    // Source of truth for invisibility is the boolean; coords are safe defaults.
    CHECK_FALSE(p.visible);
    CHECK(p.screen.x == doctest::Approx(0.0f));
    CHECK(p.screen.y == doctest::Approx(0.0f));
    CHECK(p.centered.x == doctest::Approx(0.0f));
    CHECK(p.centered.y == doctest::Approx(0.0f));
    CHECK(p.perspective_scale == doctest::Approx(0.0f));
    // Negative guard: NOT emitted as a numeric sentinel like -FLT_MAX.
    CHECK(p.screen.x > -std::numeric_limits<f32>::max() * 0.5f);
    CHECK(p.screen.y > -std::numeric_limits<f32>::max() * 0.5f);
    // Sanity for a VISIBLE point: coords are non-zero (perspective math ran).
    auto p_vis = camera_math::project_world_point(cam, Vec3{0, 0, 0}, camera_math::Viewport2D{1920, 1080});
    CHECK(p_vis.visible);
    CHECK(p_vis.screen.x == doctest::Approx(960.0f));
    CHECK(p_vis.screen.y == doctest::Approx(540.0f));
}

TEST_CASE("Camera projection contract: project_world_to_screen matches contract") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    Viewport vp{1920.0f, 1080.0f};
    Vec3 world{100, 50, 0};
    auto sp = project_world_to_screen(world, cam, vp);
    auto cp = camera_math::project_world_point(cam, world, camera_math::Viewport2D{vp.width, vp.height});
    CHECK(sp.behind_camera == !cp.visible);
    CHECK(sp.position.x == doctest::Approx(cp.screen.x).epsilon(0.001f));
    CHECK(sp.position.y == doctest::Approx(cp.screen.y).epsilon(0.001f));
    CHECK(sp.depth == doctest::Approx(cp.depth).epsilon(0.001f));
}

TEST_CASE("Camera projection contract: ProjectionContext matches contract") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    cam.point_of_interest = {0, 0, 0};
    cam.point_of_interest_enabled = true;
    auto ctx = renderer::make_projection_context(cam, 1920, 1080);
    Vec3 world{100, 50, 0};
    bool ctx_ok = false;
    Vec2 ctx_screen = ctx.project(world, ctx_ok);
    auto cp = camera_math::project_world_point(cam, world, camera_math::Viewport2D{1920, 1080});
    CHECK(ctx_ok == cp.visible);
    if (ctx_ok) {
        CHECK(ctx_screen.x == doctest::Approx(cp.screen.x).epsilon(0.1f));
        CHECK(ctx_screen.y == doctest::Approx(cp.screen.y).epsilon(0.1f));
    }
}

TEST_CASE("Camera projection contract: focal_from_camera matches legacy focal_length_from_fov") {
    const f32 h = 720.0f;
    const f32 fov = 50.0f;
    f32 legacy = focal_length_from_fov(h, fov);
    Camera2_5D tmp;
    tmp.fov_deg = fov;
    f32 contract = camera_math::focal_from_camera(tmp, 720, 720);
    CHECK(legacy == doctest::Approx(contract).epsilon(0.0001f));
}

// ============================================================================
// AE-CameraContract acceptance tests
// ============================================================================

TEST_CASE("AE-CameraContract: PhysicalLens mode changes perspective even with DoF disabled") {
    Camera2_5D wide;
    wide.enabled     = true;
    wide.optics_mode = CameraOpticsMode::PhysicalLens;
    wide.zoom        = 1000.0f;
    wide.lens         = LensPresets::full_frame_24mm();
    wide.dof.enabled            = false;
    wide.dof.use_physical_model = false;

    Camera2_5D tele;
    tele.enabled     = true;
    tele.optics_mode = CameraOpticsMode::PhysicalLens;
    tele.zoom        = 1000.0f;
    tele.lens         = LensPresets::full_frame_135mm();
    tele.dof.enabled            = false;
    tele.dof.use_physical_model = false;

    const f32 wide_focal = camera_math::focal_from_camera(wide, 1920.0f, 1080.0f);
    const f32 tele_focal = camera_math::focal_from_camera(tele, 1920.0f, 1080.0f);
    CHECK(wide_focal < tele_focal);

    CHECK(wide_focal != doctest::Approx(wide.zoom).epsilon(0.01f));
}

TEST_CASE("AE-CameraContract: LockToZoom pins focus_distance to zoom") {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);
    rig.zoom.set(750.0f);
    rig.dof.enabled        = true;
    rig.dof.focus_mode     = CameraFocusMode::LockToZoom;
    rig.dof.focus_distance.set(5000.0f);  // must be ignored

    Camera2_5D cam = rig.evaluate(Frame{0});
    CHECK(cam.dof.focus_distance == doctest::Approx(750.0f).epsilon(1e-4f));
}

TEST_CASE("AE-CameraContract: OneNode ignores target_name and stays static when no animation") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);
    rig.target_name = "";

    Camera2_5D cam = rig.evaluate(Frame{0});
    CHECK_FALSE(cam.is_animated);
    CHECK_FALSE(cam.point_of_interest_enabled);
}

TEST_CASE("AE-CameraContract: OneNode with external parent is animated (conservative fallback)") {
    Transform3D parent;
    parent.position = {0, 0, 100};
    ResolvedSceneTransforms resolved = build_projection_resolver({{"p", parent}});

    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.parent_name = "p";
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);
    CHECK(cam.is_animated);
}

TEST_CASE("AE-CameraContract: resolve_look_at_orientation is stable across vertical traversal") {
    const Vec3 camera_pos{0, 0, -1000};
    Camera2_5D cam;
    cam.position = camera_pos;
    cam.point_of_interest_enabled = true;

    auto forward_at = [](const Camera2_5D& cam) -> Vec3 {
        const Mat4 vm = cam.view_matrix();
        return glm::normalize(Vec3{vm[0][2], vm[1][2], vm[2][2]});
    };

    for (f32 target_y : {-200.0f, -100.0f, -1.0f, 0.0f, 1.0f, 100.0f, 200.0f}) {
        cam.point_of_interest = Vec3{0, target_y, 0};
        const Quat q     = cam.resolve_look_at_orientation();
        const f32  qnorm = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        CHECK(qnorm == doctest::Approx(1.0f).epsilon(1e-4f));

        const Vec3 fwd = forward_at(cam);
        const Vec3 to_target = glm::normalize(cam.point_of_interest - cam.position);
        const float dot = glm::dot(fwd, to_target);
        CHECK(dot == doctest::Approx(1.0f).epsilon(1e-3f));
        CHECK_FALSE(std::isnan(fwd.x));
        CHECK_FALSE(std::isnan(fwd.y));
        CHECK_FALSE(std::isnan(fwd.z));
        CHECK_FALSE(std::isnan(dot));
    }
}

TEST_CASE("AE-CameraContract: evaluate() is deterministic in pose and pixels") {
    Transform3D subject;
    subject.position = {100.0f, 50.0f, -1500.0f};
    Transform3D parent;
    parent.position = {0.0f, 0.0f, 0.0f};
    ResolvedSceneTransforms resolved = build_projection_resolver({
        {"subject", subject},
        {"parent",  parent},
    });

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_name = "subject";
    rig.parent_name = "parent";
    rig.orbit_radius.set(1200.0f);
    rig.zoom.set(1000.0f);
    rig.dof.enabled = true;
    rig.dof.focus_mode = CameraFocusMode::TargetLayer;
    rig.dof.focus_distance.set(99999.0f);  // ignored in TargetLayer mode

    const auto a = rig.evaluate(Frame{15}, &resolved);
    const auto b = rig.evaluate(Frame{15}, &resolved);

    CHECK(a.position == b.position);
    CHECK(a.point_of_interest == b.point_of_interest);
    CHECK(a.zoom == doctest::Approx(b.zoom));
    CHECK(a.dof.focus_distance == doctest::Approx(1200.0f).epsilon(1e-3f));
    CHECK(a.dof.focus_distance != doctest::Approx(99999.0f).epsilon(1e-3f));

    auto pa = camera_math::project_world_point(a, Vec3{0, 0, 0},
                                                {1920.0f, 1080.0f});
    auto pb = camera_math::project_world_point(b, Vec3{0, 0, 0},
                                                {1920.0f, 1080.0f});
    CHECK(pa.screen.x == doctest::Approx(pb.screen.x).epsilon(1e-5f));
    CHECK(pa.screen.y == doctest::Approx(pb.screen.y).epsilon(1e-5f));
    CHECK(pa.depth    == doctest::Approx(pb.depth).epsilon(1e-5f));
}

// ============================================================================
// CAM-03 (DOC 02) — Projection contract parity tests
//
// Verify that the focal-from-camera contract agrees between two derivation
// paths (rendering projection vs framing solver) and that the
// EvaluatedProjection snapshot is sane across Zoom / Fov / PhysicalLens.
// ============================================================================

TEST_CASE("CAM-03 parity: framing solver returns base camera -> projection is bit-exact") {
    // Camera far from any target so the framing solver has no reason to
    // apply dolly / dead-zone / safe-area adjustments.
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {10000.0f, 10000.0f, -5000.0f};
    cam.zoom = 1000.0f;
    cam.fov_deg = 50.0f;

    Vec3 world_pt{0.0f, 0.0f, 0.0f};
    auto p_direct = camera_math::project_world_point(cam, world_pt, {1920.0f, 1080.0f});

    // Drive the framing solver with empty targets; the solver must return
    // the base camera unchanged.  Projection through the framed camera
    // must be bit-exact equal to the direct path.
    CameraFramingSolver solver;
    CameraFramingRequest req;
    req.targets = {};  // empty -> base preserved
    req.viewport = {1920, 1080};
    FramingSession session;
    auto framed = solver.solve(req, cam, session);

    CHECK(framed.camera.position.x == doctest::Approx(cam.position.x).epsilon(1e-5f));
    CHECK(framed.camera.position.y == doctest::Approx(cam.position.y).epsilon(1e-5f));

    auto p_framed = camera_math::project_world_point(framed.camera, world_pt, {1920.0f, 1080.0f});
    CHECK(p_direct.screen.x == doctest::Approx(p_framed.screen.x).epsilon(1e-5f));
    CHECK(p_direct.screen.y == doctest::Approx(p_framed.screen.y).epsilon(1e-5f));
    CHECK(p_direct.depth == doctest::Approx(p_framed.depth).epsilon(1e-5f));
}

// CAM-03 / DOC 02 followup — reviewer minor #5: add the DOLLY-forward case.
//   Frame a small (point-like) target that is INSIDE the initial frustum but
//   offset from center; the framing solver must dolly the camera forward
//   (z increases — pulls camera toward target in our LH coord system) AND
//   re-aim so the target world point projects near the viewport center.
//
// Reviewer's note: the first framing parity test covers the "no motion" case
// (empty targets → bit-exact).  This second test covers the "motion + re-aim"
// case that exercises compute_dolly() + compute_aim_point() + apply_dead_zone()
// together — the canonical pipeline used by the production framing path.
TEST_CASE("CAM-03 parity: framing solver DOLLY forward + re-aim -> target projects near center") {
    // Camera offset from target.  Target at world origin is INSIDE the
    // initial frustum (camera at z=-1000, zoom=1000, fov equivalent to
    // ~50deg → half-frustum at z=0 covers ±1000 world units horizontally),
    // but offset from camera's current aim direction.
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {-200.0f, -150.0f, -1000.0f};
    cam.zoom = 1000.0f;
    cam.point_of_interest = {0.0f, 0.0f, 0.0f};
    cam.point_of_interest_enabled = true;  // initial aim at target

    Vec3 target{0.0f, 0.0f, 0.0f};

    CameraFramingRequest req;
    req.targets = {FramingBBox{target, target, 1.0f}};  // point-like BBox
    req.viewport = {1920, 1080};
    req.strategy = FramingStrategy::FitAll;
    // Disable dead zone + hysteresis so the dolly + aim are applied directly
    // (reviewer's #5 request — verifies the dolly itself, not the dampening).
    req.dead_zone.dolly_dead_zone   = 0.0f;
    req.dead_zone.aim_dead_zone_deg = 0.0f;
    req.dead_zone.hysteresis        = 0.0f;
    req.aim_error_deg               = 0.0f;  // legacy: no aim-error gating

    FramingSession session;
    auto framed = CameraFramingSolver{}.solve(req, cam, session);
    REQUIRE(framed.ok);

    // Dolly assertion: camera must move forward (z increases — dolly toward
    // target).  The exact delta magnitude depends on the framing solver's
    // scale; we verify DIRECTION + NON-ZERO, not the magnitude.
    INFO("Base z = " << cam.position.z);
    INFO("Framed z = " << framed.camera.position.z);
    CHECK(framed.camera.position.z > cam.position.z);  // dolly forward
    CHECK(std::abs(framed.camera.position.z - cam.position.z) > 0.1f);

    // After framing + re-aim, the target world point must project near
    // viewport center (960 px, 540 px) — the framing's whole point.
    auto p_target = camera_math::project_world_point(
        framed.camera, target, chronon3d::camera_math::Viewport2D{1920.0f, 1080.0f});
    REQUIRE(p_target.visible);
    INFO("Target screen = " << p_target.screen.x << "x, " << p_target.screen.y << "y");
    // 60 px tolerance covers the framing solver's residual fit error for
    // a small BBox in FitAll mode with default safe-area 12% margins.
    CHECK(p_target.screen.x == doctest::Approx(960.0f).epsilon(60.0f));
    CHECK(p_target.screen.y == doctest::Approx(540.0f).epsilon(60.0f));
}

TEST_CASE("CAM-03: PhysicalLens routes focal through lens model, not zoom") {
    Camera2_5D cam;
    cam.optics_mode = CameraOpticsMode::PhysicalLens;
    cam.lens = LensPresets::full_frame_50mm();
    cam.zoom = 1234.0f;  // intentionally different from the lens-derived focal
    const f32 focal = camera_math::focal_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(focal != doctest::Approx(1234.0f));
    CHECK(focal > 0.0f);
    CHECK_FALSE(std::isnan(focal));
}

TEST_CASE("CAM-03: focal_from_camera is non-NaN for degenerate FOV/zoom inputs") {
    Camera2_5D cam;
    cam.fov_deg = 0.0f;
    cam.zoom = 0.0f;
    cam.optics_mode = CameraOpticsMode::Zoom;
    const f32 focal = camera_math::focal_from_camera(cam, 1920.0f, 1080.0f);
    CHECK_FALSE(std::isnan(focal));
    CHECK_FALSE(std::isinf(focal));
}

TEST_CASE("CAM-03: make_evaluated_projection snapshot fields are sane (Fov mode)") {
    Camera2_5D cam;
    cam.fov_deg = 50.0f;
    cam.zoom = 1000.0f;
    cam.optics_mode = CameraOpticsMode::FieldOfView;
    auto snap = chronon3d::camera_v1::make_evaluated_projection(cam, {1920.0f, 1080.0f});
    // focal = (vp.height / 2) / tan(25°) ≈ 1158 px.
    CHECK(snap.focal_y_px == doctest::Approx(1158.0f).epsilon(5.0f));
    CHECK(snap.focal_x_px == doctest::Approx(snap.focal_y_px).epsilon(1e-4f));
    CHECK(snap.active_viewport.width  == doctest::Approx(1920.0f));
    CHECK(snap.active_viewport.height == doctest::Approx(1080.0f));
    CHECK_FALSE(snap.is_physical_lens);
    CHECK_FALSE(snap.is_anamorphic);
}


// ============================================================================
// TICKET-035 — FocalPx per-axis + GateFit::Stretch + Overscan active viewport
// + anamorphic_squeeze + pixel_aspect from LensModel.
// ============================================================================

TEST_CASE("TICKET-035: focal_xy_from_camera returns equal values for spherical Fill") {
    Camera2_5D cam;
    cam.lens = LensPresets::full_frame_50mm();   // 50mm spherical, Fill, no squeeze
    cam.optics_mode = CameraOpticsMode::PhysicalLens;
    auto fxy = camera_math::focal_xy_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(fxy.x == doctest::Approx(fxy.y).epsilon(1e-4f));
    CHECK(fxy.x > 0.0f);
    CHECK_FALSE(std::isnan(fxy.x));
    CHECK_FALSE(std::isnan(fxy.y));
}

TEST_CASE("TICKET-035: GateFit::Stretch produces focal_x != focal_y (independent axes)") {
    Camera2_5D cam;
    cam.lens = LensPresets::full_frame_50mm();
    cam.lens.gate_fit = GateFit::Stretch;        // viewport aspect != sensor aspect -> axes differ
    cam.optics_mode = CameraOpticsMode::PhysicalLens;

    // Sensor is 36x24mm (3:2 = 1.5); viewport is 1920x1080 (16:9 = ~1.78).
    // For Stretch: focal_x = 50 * (1920/36) ~= 2666.67 px
    //              focal_y = 50 * (1080/24) = 2250 px
    // => focal_x > focal_y because viewport is wider than sensor.
    auto fxy = camera_math::focal_xy_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(fxy.x != doctest::Approx(fxy.y).epsilon(0.01f));
    CHECK(fxy.x == doctest::Approx(50.0f * 1920.0f / 36.0f).epsilon(0.5f));
    CHECK(fxy.y == doctest::Approx(50.0f * 1080.0f / 24.0f).epsilon(0.5f));
}

TEST_CASE("TICKET-035: GateFit::Overscan exposes effective viewport + offset (pillarbox for 3:2 sensor in 16:9)") {
    Camera2_5D cam;
    cam.lens = LensPresets::full_frame_50mm();   // 36x24mm sensor, 1.5 aspect
    cam.lens.gate_fit = GateFit::Overscan;
    auto eff = cam.lens.effective_viewport(1920.0f, 1080.0f);
    // Viewport 16:9 (~1.78) > sensor 3:2 (1.5) => pillarbox layout.
    // Effective width  = viewport_height * sensor_aspect = 1080 * 1.5 = 1620.
    // Effective height = viewport_height = 1080.
    // Side bars are equal: x offset = (1920 - 1620) / 2 = 150 px.
    CHECK(eff.width  == doctest::Approx(1620.0f).epsilon(0.5f));
    CHECK(eff.height == doctest::Approx(1080.0f).epsilon(0.5f));
    CHECK(eff.x      == doctest::Approx(150.0f).epsilon(0.5f));   // pillarbox offset
    CHECK(eff.y      == doctest::Approx(0.0f));                   // no vertical bar

    // Principal point stays centred at the canvas centre even though the
    // active subrect is offset (150 + 1620/2 = 960 = canvas horizontal centre).
    auto snap = chronon3d::camera_v1::make_evaluated_projection(cam, {1920.0f, 1080.0f});
    CHECK(snap.principal_point_px.x == doctest::Approx(960.0f).epsilon(0.5f));
    CHECK(snap.principal_point_px.y == doctest::Approx(540.0f).epsilon(0.5f));

    // Fill and Stretch are passthroughs (x:y = 0:0, width/height = vp).
    cam.lens.gate_fit = GateFit::Fill;
    auto eff_fill = cam.lens.effective_viewport(1920.0f, 1080.0f);
    CHECK(eff_fill.x      == doctest::Approx(0.0f));
    CHECK(eff_fill.y      == doctest::Approx(0.0f));
    CHECK(eff_fill.width  == doctest::Approx(1920.0f));
    CHECK(eff_fill.height == doctest::Approx(1080.0f));

    cam.lens.gate_fit = GateFit::Stretch;
    auto eff_str = cam.lens.effective_viewport(1920.0f, 1080.0f);
    CHECK(eff_str.x      == doctest::Approx(0.0f));
    CHECK(eff_str.y      == doctest::Approx(0.0f));
    CHECK(eff_str.width  == doctest::Approx(1920.0f));
    CHECK(eff_str.height == doctest::Approx(1080.0f));
}

TEST_CASE("TICKET-035: GateFit::Overscan letterbox layout (sensor wider than portrait viewport)") {
    Camera2_5D cam;
    cam.lens = LensPresets::full_frame_50mm();   // 36x24mm sensor (sa = 1.5)
    cam.lens.gate_fit = GateFit::Overscan;
    // Vertical 1080x1920 viewport (vp_a ~= 0.5625) is narrower than sensor
    // (sa = 1.5) => letterbox layout.  Effective height = vp_w / sa = 1080/1.5
    // = 720.  Top/bottom bars equal: y offset = (1920 - 720) / 2 = 600 px.
    auto eff = cam.lens.effective_viewport(1080.0f, 1920.0f);
    CHECK(eff.x      == doctest::Approx(0.0f));                   // no horizontal bar
    CHECK(eff.y      == doctest::Approx(600.0f).epsilon(0.5f));
    CHECK(eff.width  == doctest::Approx(1080.0f).epsilon(0.5f));
    CHECK(eff.height == doctest::Approx(720.0f).epsilon(0.5f));
}

TEST_CASE("TICKET-035: anamorphic_squeeze 2.0 produces focal_x == 2 * focal_y for anamorphic_50mm") {
    Camera2_5D cam;
    cam.lens = LensPresets::anamorphic_50mm();    // 50mm anamorphic Fill, anamorphic_squeeze=2.0
    cam.optics_mode = CameraOpticsMode::PhysicalLens;
    auto fxy = camera_math::focal_xy_from_camera(cam, 1920.0f, 1080.0f);
    // For Fill with 3:2 sensor in 16:9 viewport, the post-aspect fit
    // produces an equal base focal_x/base_focal_y; anamorphic_squeeze=2 then
    // inflates focal_x by exactly 2x.
    CHECK(fxy.x == doctest::Approx(2.0f * fxy.y).epsilon(0.5f));
    CHECK(cam.lens.anamorphic_squeeze == doctest::Approx(2.0f));

    // Spherical lens with default squeeze 1.0 should NOT inflate focal_x.
    cam.lens = LensPresets::full_frame_50mm();
    auto fxy_spherical = camera_math::focal_xy_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(fxy_spherical.x == doctest::Approx(fxy_spherical.y).epsilon(1e-4f));
}

TEST_CASE("TICKET-035: EvaluatedProjection active_viewport honours Overscan; pixel_aspect + anamorphic_squeeze from LensModel") {
    Camera2_5D cam;
    cam.optics_mode = CameraOpticsMode::PhysicalLens;
    cam.lens = LensPresets::anamorphic_50mm();
    cam.lens.gate_fit = GateFit::Overscan;

    auto snap = chronon3d::camera_v1::make_evaluated_projection(cam, {1920.0f, 1080.0f});
    CHECK(snap.is_physical_lens);
    CHECK(snap.is_anamorphic);                  // squeeze=2 -> focal_x != focal_y
    CHECK(snap.focal_x_px == doctest::Approx(2.0f * snap.focal_y_px).epsilon(0.5f));
    CHECK(snap.pixel_aspect        == doctest::Approx(cam.lens.pixel_aspect));
    CHECK(snap.anamorphic_squeeze  == doctest::Approx(cam.lens.anamorphic_squeeze));
    CHECK(snap.anamorphic_squeeze  == doctest::Approx(2.0f));
    // active_viewport for 3:2 sensor in 16:9 viewport with Overscan == {1620, 1080}.
    CHECK(snap.active_viewport.width  == doctest::Approx(1620.0f).epsilon(0.5f));
    CHECK(snap.active_viewport.height == doctest::Approx(1080.0f).epsilon(0.5f));
}

// ============================================================================
// SENTINEL REGRESSION — GateFit::Overscan must NEVER return an effective
// viewport larger than the requested viewport.  Property-based: iterates a
// fixed set of (sensor, viewport) combinations; REQUIRE captures the
// offending case so a failure says exactly which combination regressed.
//
// Epsilon note: 1e-2f (one centi-pixel) accommodates f32 rounding noise at
// 4K (ULP ~= 2.4e-4f) across multiple divisions + multiplications without
// hiding genuine regressions at the sub-pixel level.
// ============================================================================
TEST_CASE("Sentinel: effective_viewport fits within requested viewport in every GateFit mode") {
    struct ViewportCase {
        f32         vp_w;
        f32         vp_h;
        const char* lens_name;
        LensModel   lens;
    };
    // Pre-built lens catalogue keeps the test body free of branching.
    const ViewportCase kCases[] = {
        // ===== Boundary: same aspect (vp_aspect == sa) — no bars in any mode =====
        {1500.0f, 1000.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // vp_a = sa = 1.5 ⇒ zero offset
        // ===== Standard viewports + full-frame 35mm (sa = 1.5) =====
        {1080.0f, 1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // 1:1 vp → LETTERBOX
        {1920.0f, 1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // 16:9 → PILLARBOX (canonical)
        {1440.0f, 1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // 4:3 vp → LETTERBOX
        {2560.0f, 1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // 21:9 → strong PILLARBOX
        {1080.0f, 1920.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // 9:16 portrait → LETTERBOX
        {3840.0f, 1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // 32:9 super wide → PILLARBOX
        // ===== 16:9 vp + alternative sensors =====
        {1920.0f, 1080.0f, "mft_25mm",        LensPresets::mft_25mm()},         // sa=1.33 → PILLARBOX
        {1920.0f, 1080.0f, "anamorphic_50mm", LensPresets::anamorphic_50mm()},  // sa=1.18 → strong PILLARBOX
        {1920.0f, 1080.0f, "imax_50mm",       LensPresets::imax_50mm()},        // sa=2.39 → LETTERBOX
        // 4K vp + ARRI Alexa 35 (sa=1.897); 4K aspect = 1.78 < 1.897 → LETTERBOX.
        {3840.0f, 2160.0f, "arri_35mm",       LensPresets::arri_35mm()},
        // ===== Edge cases =====
        {1.0f,    1.0f,    "full_frame_50mm", LensPresets::full_frame_50mm()},  // single-pixel
        {0.0f,    1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // zero width
        {1080.0f, 0.0f,    "full_frame_50mm", LensPresets::full_frame_50mm()},  // zero height
        {-1.0f,   1080.0f, "full_frame_50mm", LensPresets::full_frame_50mm()},  // negative width
    };

    for (const auto& c : kCases) {
        // ── Overscan: PRIMARY sentinel ─ the user's regression requirement. ──
        {
            Camera2_5D cam;
            cam.lens = c.lens;
            cam.lens.gate_fit = GateFit::Overscan;
            CAPTURE(c.vp_w); CAPTURE(c.vp_h); CAPTURE(c.lens_name); CAPTURE("Overscan");

            const auto eff = cam.lens.effective_viewport(c.vp_w, c.vp_h);
            // Negative inputs early-return {0,0,0,0}; clamp upper-bound so the
            // invariant `eff.width <= vp_w` doesn't false-positive on bad input.
            const f32 vp_w_eff = std::max(0.0f, c.vp_w);
            const f32 vp_h_eff = std::max(0.0f, c.vp_h);
            // Core sentinel (the regression that the user wants to catch):
            REQUIRE(eff.width  <= vp_w_eff + 1e-2f);
            REQUIRE(eff.height <= vp_h_eff + 1e-2f);
            // Subrect fits inside the canvas (no negative-offset drift):
            REQUIRE(eff.x >= -1e-2f);
            REQUIRE(eff.y >= -1e-2f);
            REQUIRE(eff.x + eff.width  <= vp_w_eff + 1e-2f);
            REQUIRE(eff.y + eff.height <= vp_h_eff + 1e-2f);
            // Non-degenerate output for non-degenerate input:
            if (c.vp_w > 0.0f && c.vp_h > 0.0f) {
                REQUIRE(eff.width  > 0.0f);
                REQUIRE(eff.height > 0.0f);
            }
        }
        // ── Fill: subrect must equal canvas for non-degenerate input (sensor cropped). ──
        {
            Camera2_5D cam;
            cam.lens = c.lens;
            cam.lens.gate_fit = GateFit::Fill;
            CAPTURE(c.vp_w); CAPTURE(c.vp_h); CAPTURE(c.lens_name); CAPTURE("Fill");

            const auto eff = cam.lens.effective_viewport(c.vp_w, c.vp_h);
            const f32 vp_w_eff = std::max(0.0f, c.vp_w);
            const f32 vp_h_eff = std::max(0.0f, c.vp_h);
            // Universal invariant: subrect never overflows the requested viewport.
            // (For vp_w <= 0 OR vp_h <= 0 the function early-returns {0,0,0,0}, which
            //  trivially satisfies the upper bound.)
            REQUIRE(eff.width  <= vp_w_eff + 1e-2f);
            REQUIRE(eff.height <= vp_h_eff + 1e-2f);
            // Equality invariant: only for non-degenerate inputs the function reaches
            // its Fill branch and returns the full canvas.
            if (c.vp_w > 0.0f && c.vp_h > 0.0f) {
                REQUIRE(eff.width  == doctest::Approx(vp_w_eff).epsilon(1e-2));
                REQUIRE(eff.height == doctest::Approx(vp_h_eff).epsilon(1e-2));
                REQUIRE(eff.x      == doctest::Approx(0.0f).epsilon(1e-2));
                REQUIRE(eff.y      == doctest::Approx(0.0f).epsilon(1e-2));
            }
        }
        // ── Stretch: subrect must equal canvas for non-degenerate input
        //    (per-axis focal can differ; size + no-offset stay canonical
        //    because Stretch intentionally fills the viewport). ──
        {
            Camera2_5D cam;
            cam.lens = c.lens;
            cam.lens.gate_fit = GateFit::Stretch;
            CAPTURE(c.vp_w); CAPTURE(c.vp_h); CAPTURE(c.lens_name); CAPTURE("Stretch");

            const auto eff = cam.lens.effective_viewport(c.vp_w, c.vp_h);
            const f32 vp_w_eff = std::max(0.0f, c.vp_w);
            const f32 vp_h_eff = std::max(0.0f, c.vp_h);
            // Universal invariant (see Fill block above for rationale).
            REQUIRE(eff.width  <= vp_w_eff + 1e-2f);
            REQUIRE(eff.height <= vp_h_eff + 1e-2f);
            // Equality invariant: only for non-degenerate inputs.
            if (c.vp_w > 0.0f && c.vp_h > 0.0f) {
                REQUIRE(eff.width  == doctest::Approx(vp_w_eff).epsilon(1e-2));
                REQUIRE(eff.height == doctest::Approx(vp_h_eff).epsilon(1e-2));
                REQUIRE(eff.x      == doctest::Approx(0.0f).epsilon(1e-2));
                REQUIRE(eff.y      == doctest::Approx(0.0f).epsilon(1e-2));
            }
        }
    }
}
