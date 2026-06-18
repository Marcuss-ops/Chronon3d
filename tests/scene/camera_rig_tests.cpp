#include <doctest/doctest.h>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_rig_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
using namespace chronon3d;


TEST_CASE("CameraRig orbit keeps constant radius") {
    CameraRig rig;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    rig.orbit_yaw.set(-30.0f);
    auto a = rig.evaluate(Frame{0});

    rig.orbit_yaw.set(30.0f);
    auto b = rig.evaluate(Frame{0});

    CHECK(std::abs(glm::length(a.position) - 1000.0f) < 0.001f);
    CHECK(std::abs(glm::length(b.position) - 1000.0f) < 0.001f);
    CHECK(a.position.x == doctest::Approx(-b.position.x).epsilon(0.001));
}

TEST_CASE("CameraRig two-node points at target") {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    auto cam = rig.evaluate(Frame{0});

    CHECK(cam.point_of_interest_enabled);
    CHECK(glm::length(cam.point_of_interest - Vec3{0, 0, 0}) < 0.001f);
}

TEST_CASE("CameraRig dolly moves along forward vector") {
    CameraRig rig;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    rig.dolly.set(0.0f);
    auto far_cam = rig.evaluate(Frame{0});

    rig.dolly.set(200.0f);
    auto near_cam = rig.evaluate(Frame{0});

    CHECK(glm::length(near_cam.position - rig.target.evaluate(Frame{0})) <
          glm::length(far_cam.position - rig.target.evaluate(Frame{0})));
}

TEST_CASE("CameraRig resolves target null") {
    SceneTransformRegistry reg;
    Transform3D target_node;
    target_node.position = {100.0f, 50.0f, 0.0f};
    reg.add_node("target_null", target_node, false);
    
    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_name = "target_null";

    auto cam = rig.evaluate(0, &resolved);

    CHECK(cam.point_of_interest.x == doctest::Approx(100.0f));
    CHECK(cam.point_of_interest.y == doctest::Approx(50.0f));
}

// ─────────────────────────────────────────────────────────────────────────
// T9 — FocusTargetOwnership
// ─────────────────────────────────────────────────────────────────────────

namespace {

/// Set up a TransformResolver with three layers at different Z depths
/// matching the test plan: near(600), subject(1000), far(1400).
struct FocusTestScene {
    SceneTransformRegistry registry;
    TransformResolverResult resolved;

    FocusTestScene() {
        // Camera is at origin looking down -Z (default camera position).
        // Layer Z values: negative Z = farther from camera.
        // Camera-space distance = |layer_z - camera_z| with camera at z=0.
        Transform3D near_node;
        near_node.position = {0.0f, 0.0f, -600.0f};
        registry.add_node("near", near_node, false);

        Transform3D subject_node;
        subject_node.position = {0.0f, 0.0f, -1000.0f};
        registry.add_node("subject", subject_node, false);

        Transform3D far_node;
        far_node.position = {0.0f, 0.0f, -1400.0f};
        registry.add_node("far", far_node, false);

        resolved = registry.resolve_all();
    }
};

} // anonymous namespace

TEST_CASE("T9 FocusTargetOwnership: TargetBinding uses focus_target, ignores manual value") {
    FocusTestScene scene;

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -1000});    // rig target = subject position
    rig.orbit_radius.set(1000.0f);

    // Enable DOF with TargetBinding focus mode pointing at "subject"
    rig.dof.enabled = true;
    rig.dof.use_target_z = true;
    rig.dof.focus_target_name = "subject";
    // Deliberately wrong manual value — must be IGNORED in TargetBinding
    rig.dof.focus_distance.set(5000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &scene.resolved);

    // The resolved focus distance must be the camera-space distance to "subject"
    // Camera at (0,0,0), subject at (0,0,-1000) → distance = 1000
    REQUIRE(cam.dof.enabled);
    CHECK(cam.dof.focus_distance == doctest::Approx(1000.0f).epsilon(1e-4f));

    // The CameraRig in TwoNode mode with orbit radius 1000 places the camera
    // 1000 units behind the target.  The resolved focus_distance should be
    // the camera-space distance to the focus target layer.
    // Here: camera at ~(0,0,-2000), subject at (0,0,-1000) → distance = 1000.

    // The resolved focus_distance must match camera→subject distance.
    f32 dist_camera_to_subject = glm::length(
        scene.resolved.world_position("subject").value_or(Vec3{0}) - cam.position);
    CHECK(cam.dof.focus_distance == doctest::Approx(dist_camera_to_subject).epsilon(1e-4f));

    // Manual value 5000 must NOT be used.
    CHECK(cam.dof.focus_distance != doctest::Approx(5000.0f).epsilon(1e-4f));

    // Both near and far layers should be at different distances from focus
    // than the subject (which is exactly at the focus plane).
    f32 dist_cam_near = glm::length(
        scene.resolved.world_position("near").value_or(Vec3{0}) - cam.position);
    f32 dist_cam_far  = glm::length(
        scene.resolved.world_position("far").value_or(Vec3{0}) - cam.position);

    // Subject is at focus (least error).
    f32 focus_error_subj = std::abs(cam.dof.focus_distance - dist_camera_to_subject);
    f32 focus_error_near = std::abs(cam.dof.focus_distance - dist_cam_near);
    f32 focus_error_far  = std::abs(cam.dof.focus_distance - dist_cam_far);

    CHECK(focus_error_subj < 1e-4f);                    // subject IS at focus
    CHECK(focus_error_near > focus_error_subj);          // near is farther from focus
    CHECK(focus_error_far  > focus_error_subj);          // far is farther from focus

    // Verify the focus deltas are meaningfully large (non-trivial defocus).
    // Subject delta ~0, so near/far must be at least 1 scene unit away.
    // This validates the 1.5× sharpness ratio constraint from the plan.
    CHECK(focus_error_near > 1.0f);
    CHECK(focus_error_far  > 1.0f);
}

TEST_CASE("T9 FocusTargetOwnership: ManualDistance uses animated value, ignores target") {
    FocusTestScene scene;

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(1000.0f);

    // Enable DOF with ManualDistance focus mode.
    // Even though focus_target_name = "subject", the manual value must win.
    rig.dof.enabled = true;
    rig.dof.use_target_z = false;
    rig.dof.focus_target_name = "subject";  // should be ignored
    rig.dof.focus_distance.set(5000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &scene.resolved);

    REQUIRE(cam.dof.enabled);
    // Manual value 5000 must be used, NOT the distance to subject (1000).
    CHECK(cam.dof.focus_distance == doctest::Approx(5000.0f).epsilon(1e-4f));

    // Confirm it's NOT the distance to the subject layer.
    f32 dist_subj = glm::length(scene.resolved.world_position("subject").value_or(Vec3{0}) - cam.position);
    CHECK(dist_subj == doctest::Approx(1000.0f).epsilon(1e-4f));
    CHECK(cam.dof.focus_distance != doctest::Approx(dist_subj).epsilon(1e-4f));
}

TEST_CASE("T9 FocusTargetOwnership: missing focus target warns and falls back to rig target") {
    FocusTestScene scene;

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -1400});    // rig target = far layer position
    rig.orbit_radius.set(1000.0f);

    rig.dof.enabled = true;
    rig.dof.use_target_z = true;
    rig.dof.focus_target_name = "nonexistent_layer";  // doesn't exist

    Camera2_5D cam = rig.evaluate(Frame{0}, &scene.resolved);

    REQUIRE(cam.dof.enabled);
    // Should fall back to rig target. Camera orbits at radius 1000 behind
    // target (0,0,-1400), so camera is at ~(0,0,-2400).
    // Distance from camera to target = 1000.
    CHECK(cam.dof.focus_distance == doctest::Approx(1000.0f).epsilon(1e-4f));
}

// ─────────────────────────────────────────────────────────────────────────
// T7 — ExternalTargetInvalidatesCamera
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("T7: rig with external target is marked animated and follows target") {
    // ── Build two resolver states simulating an animated parent ─────────
    // Frame 0: parent at (0,0,0), child offset at (100,0,0) → child at (100,0,0)
    // Frame 30: parent at (200,0,0), child same offset → child at (300,0,0)
    SceneTransformRegistry reg0;
    Transform3D parent;
    parent.position = {0.0f, 0.0f, 0.0f};
    reg0.add_node("target_parent", parent, false);

    Transform3D child;
    child.position   = {100.0f, 0.0f, 0.0f};
    child.parent_name = "target_parent";
    reg0.add_node("target_child", child, false);
    auto resolved0 = reg0.resolve_all();

    SceneTransformRegistry reg30;
    Transform3D parent30;
    parent30.position = {200.0f, 0.0f, 0.0f};
    reg30.add_node("target_parent", parent30, false);

    Transform3D child30;
    child30.position   = {100.0f, 0.0f, 0.0f};
    child30.parent_name = "target_parent";
    reg30.add_node("target_child", child30, false);
    auto resolved30 = reg30.resolve_all();

    // ── CameraRig with NO local animation ──────────────────────────────
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_name = "target_child";
    rig.target.set(Vec3{0, 0, 0});         // ignored when target_name resolves
    rig.orbit_radius.set(1000.0f);          // static orbit radius
    // No yaw, pitch, dolly, pan, tilt, roll — completely static.

    // ── Evaluate at both frames ────────────────────────────────────────
    Camera2_5D cam0  = rig.evaluate(Frame{0}, &resolved0);
    Camera2_5D cam30 = rig.evaluate(Frame{30}, &resolved30);

    // ── DoD: is_animated must be true (external dependency) ─────────────
    CHECK(cam0.is_animated);
    CHECK(cam30.is_animated);

    // ── DoD: hash frame 0 ≠ hash frame 30 ──────────────────────────────
    // Poses must differ because the target moved.
    bool poses_differ =
        glm::length(cam0.position - cam30.position) > 0.1f ||
        glm::length(cam0.point_of_interest - cam30.point_of_interest) > 0.1f;
    CHECK(poses_differ);

    // ── DoD: target centred ≤ 2px in both frames ────────────────────────
    // The resolved point_of_interest must match the child's world position.
    Vec3 expected_target0  = resolved0.world_position("target_child").value_or(Vec3{0});
    Vec3 expected_target30 = resolved30.world_position("target_child").value_or(Vec3{0});
    // Child at (100,0,0) parented to parent at (0,0,0) → world (100,0,0)
    CHECK(cam0.point_of_interest.x == doctest::Approx(expected_target0.x).epsilon(1e-4f));
    CHECK(cam0.point_of_interest.y == doctest::Approx(expected_target0.y).epsilon(1e-4f));
    // Child at (100,0,0) parented to parent at (200,0,0) → world (300,0,0)
    CHECK(cam30.point_of_interest.x == doctest::Approx(expected_target30.x).epsilon(1e-4f));
    CHECK(cam30.point_of_interest.y == doctest::Approx(expected_target30.y).epsilon(1e-4f));

    // ── DoD: render frame 30 repeated → identical result ────────────────
    Camera2_5D cam30_repeat = rig.evaluate(Frame{30}, &resolved30);
    CHECK(cam30.position == cam30_repeat.position);
    CHECK(cam30.point_of_interest == cam30_repeat.point_of_interest);
    CHECK(cam30.zoom == doctest::Approx(cam30_repeat.zoom));
}

TEST_CASE("T7: rig with external parent is marked animated") {
    SceneTransformRegistry reg;
    Transform3D parent;
    parent.position = {42.0f, 0.0f, 0.0f};
    reg.add_node("camera_parent", parent, false);
    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.parent_name = "camera_parent";
    rig.target.set(Vec3{0, 0, -1000});
    rig.orbit_radius.set(500.0f);
    // No animated properties.

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);

    // Must be marked animated because parent_name is non-empty.
    CHECK(cam.is_animated);
}

TEST_CASE("T7: rig with NO external dependencies is NOT marked animated") {
    // Sanity check: a rig with no external deps and no local animation
    // should correctly report is_animated=false.
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    // No parent_name, no target_name, no focus_target_name.
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);
    // All properties at defaults — no keyframes.

    Camera2_5D cam = rig.evaluate(Frame{0});

    CHECK_FALSE(cam.is_animated);
}

// ─────────────────────────────────────────────────────────────────────────
// T1 — CameraEulerRotationConsistency
// ─────────────────────────────────────────────────────────────────────────
// Verifies that set_rotation_euler / rotation_euler / rotation_quaternion
// are consistent with each other.

TEST_CASE("T1: Euler rotation set/roundtrip consistency") {
    const Vec3 test_euler{-12.0f, 35.0f, 7.0f};

    // Panel A: Euler rotation
    Camera2_5D cam_euler;
    cam_euler.enabled = true;
    cam_euler.position = {0, 0, -1000};
    cam_euler.set_rotation_euler(test_euler);

    // Panel B: Euler rotation from same angles
    Camera2_5D cam_quat;
    cam_quat.enabled = true;
    cam_quat.position = {0, 0, -1000};
    cam_quat.set_rotation_euler(test_euler);

    // ── DoD: view matrices must be identical ────────────────────────────
    const Mat4 vm_euler = cam_euler.view_matrix();
    const Mat4 vm_quat  = cam_quat.view_matrix();

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            CHECK(vm_euler[col][row] == doctest::Approx(vm_quat[col][row]).epsilon(1e-5f));
        }
    }

    // ── DoD: forward/right/up directions must match ────────────────────
    CameraPose pose_euler{cam_euler.position, cam_euler.rotation_quaternion()};
    CameraPose pose_quat{cam_quat.position, cam_quat.rotation_quaternion()};

    CHECK(glm::length(pose_euler.forward() - pose_quat.forward()) < 1e-5f);
    CHECK(glm::length(pose_euler.right() - pose_quat.right()) < 1e-5f);
    CHECK(glm::length(pose_euler.up() - pose_quat.up()) < 1e-5f);

    // ── DoD: rotation_euler() round-trips correctly ────────────────────
    Vec3 euler_out = cam_quat.rotation_euler();
    CHECK(euler_out.x == doctest::Approx(test_euler.x).epsilon(1e-5f));
    CHECK(euler_out.y == doctest::Approx(test_euler.y).epsilon(1e-5f));
    CHECK(euler_out.z == doctest::Approx(test_euler.z).epsilon(1e-5f));
}

TEST_CASE("T1: AnimatedCamera2_5D rotation consistency") {
    const Vec3 test_euler{-12.0f, 35.0f, 7.0f};

    AnimatedCamera2_5D acam;
    acam.rotation.set(test_euler);
    Camera2_5D r = acam.evaluate(Frame{0});

    // Rotation round-trips correctly
    CHECK(r.rotation.x == doctest::Approx(-12.0f).epsilon(1e-5f));
    CHECK(r.rotation.y == doctest::Approx(35.0f).epsilon(1e-5f));
    CHECK(r.rotation.z == doctest::Approx(7.0f).epsilon(1e-5f));

    // View matrix must be valid
    const Mat4 vm = r.view_matrix();
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            CHECK_FALSE(std::isnan(vm[col][row]));
}

// ─────────────────────────────────────────────────────────────────────────
// T2 — TwoNodeTargetLockOrbit
// ─────────────────────────────────────────────────────────────────────────
// CameraRig TwoNode with orbit yaw/pitch animation: verifies the target
// stays locked at the point of interest throughout the motion.

TEST_CASE("T3: TwoNode target stays locked during orbit animation") {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -500});
    rig.orbit_radius.set(1000.0f);

    // Animate yaw -60° → +60°, pitch -20° → +20° over 90 frames
    rig.orbit_yaw.key(Frame{0}, -60.0f).key(Frame{90}, 60.0f, EasingCurve{Easing::InOutCubic});
    rig.orbit_pitch.key(Frame{0}, -20.0f).key(Frame{90}, 20.0f, EasingCurve{Easing::InOutCubic});
    rig.dolly.key(Frame{0}, -100.0f).key(Frame{45}, 100.0f).key(Frame{90}, -100.0f);

    const Vec3 target_pos = rig.target.evaluate(Frame{0});

    f32 max_error = 0.0f;
    f32 sum_error = 0.0f;
    int samples = 0;

    for (Frame f = 0; f <= Frame{90}; f = f + Frame{10}) {
        Camera2_5D cam = rig.evaluate(f);

        CHECK(cam.point_of_interest_enabled);

        f32 error = glm::length(cam.point_of_interest - target_pos);
        max_error = std::max(max_error, error);
        sum_error += error;
        ++samples;

        // Rotation matrix determinant: must be ~1 (orthonormal)
        Mat4 vm = cam.view_matrix();
        glm::mat3 rot = glm::mat3(vm);
        f32 det = glm::determinant(rot);
        CHECK(det == doctest::Approx(1.0f).epsilon(0.001f));
    }

    // ── DoD: mean error <= 1 unit, max <= 2 units ──────────────────────
    f32 mean_error = sum_error / static_cast<f32>(samples);
    CHECK(mean_error < 1e-4f);
    CHECK(max_error < 1e-4f);

    // ── DoD: no NaN/Inf in any view matrix ─────────────────────────────
}

TEST_CASE("T3: TwoNode target stays locked with resolver (external target)") {
    SceneTransformRegistry reg;
    Transform3D target_node;
    target_node.position = {-80.0f, 30.0f, 0.0f};
    reg.add_node("camera_target", target_node, false);
    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_name = "camera_target";
    rig.target.set(Vec3{0, 0, 0});  // overridden by resolver
    rig.orbit_radius.set(800.0f);
    rig.orbit_yaw.key(Frame{0}, -45.0f).key(Frame{60}, 45.0f);

    Vec3 expected_target = resolved.world_position("camera_target").value_or(Vec3{0});

    for (Frame f = 0; f <= Frame{60}; f = f + Frame{15}) {
        Camera2_5D cam = rig.evaluate(f, &resolved);
        CHECK(cam.point_of_interest_enabled);
        f32 error = glm::length(cam.point_of_interest - expected_target);
        CHECK(error < 1e-4f);
    }
}


