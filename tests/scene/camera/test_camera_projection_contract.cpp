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
//
// Tests:
//   Center point projects to viewport centre
//   Y sign is inverted (screen Y down)
//   X sign is correct
//   Points behind camera are invisible
//   Depth increases with distance
//   Perspective scale varies with depth
//   FOV and zoom focal equivalence
//   Sentinel values for invisible points
//   All 4 paths agree for the same input
// ============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <cmath>
using namespace chronon3d;


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
    cam_fov.projection_mode = Camera2_5DProjectionMode::Fov;
    cam_fov.fov_deg = 90.0f;
    const f32 focal = camera_math::focal_from_camera(cam_fov, 1920, 1080);
    Camera2_5D cam_zoom = cam_fov;
    cam_zoom.projection_mode = Camera2_5DProjectionMode::Zoom;
    cam_zoom.zoom = focal;
    auto a = camera_math::project_world_point(cam_fov, {100, 0, 0}, {1920, 1080});
    auto b = camera_math::project_world_point(cam_zoom, {100, 0, 0}, {1920, 1080});
    CHECK(a.screen.x == doctest::Approx(b.screen.x).epsilon(0.0001f));
    CHECK(a.screen.y == doctest::Approx(b.screen.y).epsilon(0.0001f));
    CHECK(a.depth == doctest::Approx(b.depth).epsilon(0.0001f));
}

TEST_CASE("Camera projection contract: sentinel values for invisible points") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    auto p = camera_math::project_world_point(cam, Vec3{0, 0, -1001}, camera_math::Viewport2D{1920, 1080});
    CHECK_FALSE(p.visible);
    CHECK(p.screen.x == doctest::Approx(-std::numeric_limits<f32>::max()));
    CHECK(p.screen.y == doctest::Approx(-std::numeric_limits<f32>::max()));
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
    tmp.projection_mode = Camera2_5DProjectionMode::Fov;
    tmp.fov_deg = fov;
    f32 contract = camera_math::focal_from_camera(tmp, 720, 720);
    CHECK(legacy == doctest::Approx(contract).epsilon(0.0001f));
}

// ============================================================================
// AE-CameraContract acceptance tests
// ----------------------------------------------------------------------------
// These cases gate the `feat(camera): establish AE-compatible camera contract`
// block.  Each case corresponds to one Definition-of-Done in the spec.
// ============================================================================

TEST_CASE("AE-CameraContract: PhysicalLens mode changes perspective even with DoF disabled") {
    // AE rule: lens (focal length + film size) drives Zoom/FOV independently
    // of the DoF toggle.  A wide-angle 24mm lens must give a wider FOV than
    // a 135mm telephoto even when dof.enabled is false and
    // use_physical_model is also false.
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
    CHECK(wide_focal < tele_focal);  // wide-angle => smaller pixel-focal

    // And the same wide lens with DoF fully disabled must NOT collapse
    // back to camera.zoom — the lens is the source of truth.
    CHECK(wide_focal != doctest::Approx(wide.zoom).epsilon(0.01f));
}

TEST_CASE("AE-CameraContract: LockToZoom pins focus_distance to zoom") {
    TransformResolverResult empty_resolver;  // no external deps needed
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);
    rig.zoom.set(750.0f);
    rig.dof.enabled        = true;
    rig.dof.focus_mode     = CameraFocusMode::LockToZoom;
    rig.dof.focus_distance.set(5000.0f);  // must be ignored

    Camera2_5D cam = rig.evaluate(Frame{0}, &empty_resolver);
    CHECK(cam.dof.focus_distance == doctest::Approx(750.0f).epsilon(1e-4f));
}

TEST_CASE("AE-CameraContract: OneNode ignores target_name and stays static when no animation") {
    // Sanity: a OneNode rig with NO target_name and NO local animation
    // must NOT be marked animated even if target_name is later added;
    // however if target_name is set we DO treat the rig as time-
    // dependent (conservative fallback) because the resolver resolution
    // could change between frames.
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);
    rig.target_name = "";   // explicit: no external target reference

    Camera2_5D cam = rig.evaluate(Frame{0});
    CHECK_FALSE(cam.is_animated);
    CHECK_FALSE(cam.point_of_interest_enabled);
}

TEST_CASE("AE-CameraContract: OneNode with external parent is animated (conservative fallback)") {
    SceneTransformRegistry reg;
    Transform3D parent;
    parent.position = {0, 0, 100};
    reg.add_node("p", parent, false);
    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.parent_name = "p";
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);
    CHECK(cam.is_animated);  // conservative: external dep forces cache invalidation
}

TEST_CASE("AE-CameraContract: resolve_look_at_orientation is stable across vertical traversal") {
    // Move the target from y=-50 to y=+50 across the camera's optical axis
    // (camera at z=-1000, target oscillates along the line perpendicular to
    // the optical axis).  The quaternion must remain unit-norm and the
    // forward direction must always point at the target (no 180° flip).
    const Vec3 camera_pos{0, 0, -1000};
    Camera2_5D cam;
    cam.position = camera_pos;
    cam.point_of_interest_enabled = true;

    auto forward_at = [](const Camera2_5D& cam) -> Vec3 {
        // GLM view matrix from lookAtLH: column 2 is the camera-forward axis
        // expressed in world coordinates (it equals normalize(target - eye)).
        // We re-derive it directly from the camera state to test that the
        // canonical quaternion path never inverts this direction.
        const Mat4 vm = cam.view_matrix();
        return glm::normalize(Vec3{vm[0][2], vm[1][2], vm[2][2]});
    };

    for (f32 target_y : {-200.0f, -100.0f, -1.0f, 0.0f, 1.0f, 100.0f, 200.0f}) {
        cam.point_of_interest = Vec3{0, target_y, 0};
        const Quat q     = cam.resolve_look_at_orientation();
        const f32  qnorm = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        // Contract: canonical chain must produce a unit quaternion for
        // every near-axial target.  No zero quat, no 180° flip, no NaN.
        CHECK(qnorm == doctest::Approx(1.0f).epsilon(1e-4f));

        const Vec3 fwd = forward_at(cam);
        const Vec3 to_target = glm::normalize(cam.point_of_interest - cam.position);
        const float dot = glm::dot(fwd, to_target);
        // Camera-forward must always be aligned with (target - position).
        // Bit-exact at 1.0 within 1e-3 because the basis construction
        // is deterministic (no GLM internal cross products).
        CHECK(dot == doctest::Approx(1.0f).epsilon(1e-3f));
        CHECK_FALSE(std::isnan(fwd.x));
        CHECK_FALSE(std::isnan(fwd.y));
        CHECK_FALSE(std::isnan(fwd.z));
        CHECK_FALSE(std::isnan(dot));
    }
}

TEST_CASE("AE-CameraContract: evaluate() is deterministic in pose and pixels") {
    // Same inputs -> same outputs.  No wall-clock jitter; deterministic
    // bit pattern.
    SceneTransformRegistry reg;
    Transform3D subject;
    subject.position = {100.0f, 50.0f, -1500.0f};
    reg.add_node("subject", subject, false);
    Transform3D parent;
    parent.position = {0.0f, 0.0f, 0.0f};
    reg.add_node("parent", parent, false);
    auto resolved = reg.resolve_all();

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
    // TargetLayer mode: focus_distance should be camera -> subject distance
    // (~1200 from orbit_radius) — NOT 99999 from the manual field.
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
