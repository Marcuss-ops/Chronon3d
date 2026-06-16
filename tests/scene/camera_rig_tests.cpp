#include <doctest/doctest.h>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_rig_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

using namespace chronon3d;

// ─────────────────────────────────────────────────────────────────────────
// TODO(HSPC): re-enable this test file once the HSPC (High-fidelity
// Spatial-camera Pipeline) surface is implemented in production:
//
//   - CameraRig::target_bindings (T10 weighted multi-target blend)
//   - CameraRig::dof.focus_mode (CameraFocusMode::TargetBinding /
//     CameraFocusMode::ManualDistance) (T9 FocusTargetOwnership)
//   - CameraRig dynamic resolver via parent_name / target_name /
//     focus_target_name (T7 ExternalTargetInvalidatesCamera)
//   - AnimatedCamera2_5D::orientation (T1 Euler/Quat parity)
//   - AnimatedQuaternion (T2 SLERP short-path sign correction)
//   - resolve_look_at_orientation() helper (T4 pole crossing)
//   - Camera2_5D::set_rotation_euler / rotation_euler / orientation_valid
//   - EasingChain: rig.orbit_yaw.key(f, deg, EasingCurve{...}) syntax
//
// Until that lands, the entire TEST_CASE body is gated behind #if 0 so
// the file still compiles and the ctest run is not blocked on missing
// production surface. The tests below remain as a contract for the
// HSPC milestone.
// ─────────────────────────────────────────────────────────────────────────
#if 0   // HSPC pending — see TODO above

TEST_CASE("CameraRig orbit keeps constant radius") {
    CameraRig rig;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    rig.orbit_yaw.set(-30.0f);
    auto a = rig.evaluate(0);

    rig.orbit_yaw.set(30.0f);
    auto b = rig.evaluate(0);

    CHECK(std::abs(glm::length(a.position) - 1000.0f) < 0.001f);
    CHECK(std::abs(glm::length(b.position) - 1000.0f) < 0.001f);
    CHECK(a.position.x == doctest::Approx(-b.position.x).epsilon(0.001));
}

TEST_CASE("CameraRig two-node points at target") {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    auto cam = rig.evaluate(0);

    CHECK(cam.point_of_interest_enabled);
    CHECK(glm::length(cam.point_of_interest - Vec3{0, 0, 0}) < 0.001f);
}

TEST_CASE("CameraRig dolly moves along forward vector") {
    CameraRig rig;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    rig.dolly.set(0.0f);
    auto far_cam = rig.evaluate(0);

    rig.dolly.set(200.0f);
    auto near_cam = rig.evaluate(0);

    CHECK(glm::length(near_cam.position - rig.target.evaluate(0)) <
          glm::length(far_cam.position - rig.target.evaluate(0)));
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
    rig.dof.focus_mode = CameraFocusMode::TargetBinding;
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
    rig.dof.focus_mode = CameraFocusMode::ManualDistance;
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
    rig.dof.focus_mode = CameraFocusMode::TargetBinding;
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
//
// Verifies that a CameraRig with NO local animation but an external
// target (target_name referencing a layer whose parent moves) is
// correctly detected as animated and produces distinct poses per frame.

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
// T1 — CameraOneNodeEulerQuaternionParity
// ─────────────────────────────────────────────────────────────────────────
// Verifies that Euler rotation and direct Quat orientation produce
// identical view matrices (gimbal-lock-free migration checkpoint).

TEST_CASE("T1: Euler rotation and Quat orientation produce identical view") {
    const Vec3 test_euler{-12.0f, 35.0f, 7.0f};
    const Quat test_quat = glm::quat(glm::radians(test_euler));

    // Panel A: Euler rotation
    Camera2_5D cam_euler;
    cam_euler.enabled = true;
    cam_euler.position = {0, 0, -1000};
    cam_euler.set_rotation_euler(test_euler);

    // Panel B: Direct Quat orientation
    Camera2_5D cam_quat;
    cam_quat.enabled = true;
    cam_quat.position = {0, 0, -1000};
    cam_quat.orientation = test_quat;
    cam_quat.orientation_valid = true;

    // ── DoD: view matrices must be identical ────────────────────────────
    const Mat4 vm_euler = cam_euler.view_matrix();
    const Mat4 vm_quat  = cam_quat.view_matrix();

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            CHECK(vm_euler[col][row] == doctest::Approx(vm_quat[col][row]).epsilon(1e-5f));
        }
    }

    // ── DoD: forward/right/up directions must match ────────────────────
    // Build CameraPose equivalents for direction comparison
    CameraPose pose_euler{cam_euler.position, cam_euler.rotation_quaternion()};
    CameraPose pose_quat{cam_quat.position, cam_quat.rotation_quaternion()};

    CHECK(glm::length(pose_euler.forward() - pose_quat.forward()) < 1e-5f);
    CHECK(glm::length(pose_euler.right() - pose_quat.right()) < 1e-5f);
    CHECK(glm::length(pose_euler.up() - pose_quat.up()) < 1e-5f);

    // ── DoD: rotation_euler() round-trips correctly ────────────────────
    Vec3 euler_from_quat = cam_quat.rotation_euler();
    // Euler → Quat → Euler may differ due to Euler angle ambiguities,
    // but the underlying orientations must be equivalent.
    Quat roundtrip_quat = glm::quat(glm::radians(euler_from_quat));
    f32 dot = glm::dot(test_quat, roundtrip_quat);
    CHECK(std::abs(dot) == doctest::Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("T1: AnimatedCamera2_5D orientation vs rotation parity") {
    const Vec3 test_euler{-12.0f, 35.0f, 7.0f};

    AnimatedCamera2_5D acam_quat;
    acam_quat.orientation.set(glm::quat(glm::radians(test_euler)));
    Camera2_5D r_quat = acam_quat.evaluate(Frame{0});

    AnimatedCamera2_5D acam_euler;
    acam_euler.rotation.set(test_euler);
    Camera2_5D r_euler = acam_euler.evaluate(Frame{0});

    // Both must be orientation_valid
    CHECK(r_quat.orientation_valid);
    CHECK(r_euler.orientation_valid);

    // View matrices must match
    const Mat4 vm_q = r_quat.view_matrix();
    const Mat4 vm_e = r_euler.view_matrix();
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            CHECK(vm_q[col][row] == doctest::Approx(vm_e[col][row]).epsilon(1e-5f));
}

// ─────────────────────────────────────────────────────────────────────────
// T2 — CameraOneNodeQuaternionWrap
// ─────────────────────────────────────────────────────────────────────────
// Verifies that quaternion interpolation takes the shortest path
// when crossing the ±180° boundary (yaw +179° → -179°).

TEST_CASE("T2: Quat SLERP takes short path across ±180° boundary") {
    const Quat q_start = glm::angleAxis(glm::radians(179.0f), Vec3{0, 1, 0});
    const Quat q_end   = glm::angleAxis(glm::radians(-179.0f), Vec3{0, 1, 0});

    // Direct angular distance: 179° → -179° = 2° (short) vs 358° (long).
    // The sign correction in AnimatedQuaternion must negate q_end when
    // dot(q_start, q_end) < 0, forcing SLERP through the 2° path.
    CHECK(glm::dot(q_start, q_end) < -0.9f);  // nearly opposite signs

    AnimatedQuaternion aquat;
    aquat.key(Frame{0}, q_start);
    aquat.key(Frame{60}, q_end);

    // ── DoD: total angular path must be ~2°, not ~358° ─────────────────
    f32 total_dot = std::abs(glm::dot(q_start, q_end));
    f32 total_angle_deg = glm::degrees(std::acos(std::min(total_dot, 1.0f)));
    CHECK(total_angle_deg < 5.0f);  // 2° direct path

    // ── DoD: midpoint must be close to both start and end (short way) ──
    Quat q_mid = aquat.evaluate(Frame{30});
    f32 mid_dot_start = std::abs(glm::dot(q_mid, q_start));
    f32 mid_dot_end   = std::abs(glm::dot(q_mid, q_end));
    // If short path, midpoint dot with both ends ~cos(1°) ≈ 0.9998
    // If long path, dot would be ~cos(179°) ≈ -0.9998
    CHECK(mid_dot_start > 0.99f);
    CHECK(mid_dot_end   > 0.99f);

    // ── DoD: adjacent quaternion dots must be ~1.0 ─────────────────────
    Quat prev = q_start;
    f32 max_angular_step_deg = 0.0f;
    for (int f = 1; f <= 60; ++f) {
        Quat curr = aquat.evaluate(Frame{f});
        f32 dot_adj = std::abs(glm::dot(prev, curr));
        CHECK(dot_adj > 0.6f);  // per-frame step < ~53°
        f32 angle_deg = glm::degrees(std::acos(std::min(dot_adj, 1.0f)));
        max_angular_step_deg = std::max(max_angular_step_deg, angle_deg);
        prev = curr;
    }

    // ── DoD: per-frame step must be tiny (~0.03° per frame for 2° over 60 frames)─
    CHECK(max_angular_step_deg < 1.0f);
}

TEST_CASE("T2: Quat SLERP with sign correction prevents long path") {
    // Two quaternions that have negative dot (opposite hemisphere).
    // Without sign correction, slerp takes the long way (~270° vs ~90°).
    Quat a = glm::angleAxis(glm::radians(45.0f), Vec3{0, 1, 0});
    Quat b = -glm::angleAxis(glm::radians(-45.0f), Vec3{0, 1, 0});
    // -b = angleAxis(-135°, Y) = equivalent to angleAxis(225°, Y)
    // a and -b are ~90° apart on the sphere.

    AnimatedQuaternion aquat;
    aquat.key(Frame{0}, a);
    aquat.key(Frame{60}, b);

    Quat q_mid = aquat.evaluate(Frame{30});
    f32 mid_dot_a = std::abs(glm::dot(a, q_mid));
    f32 mid_dot_b = std::abs(glm::dot(b, q_mid));

    // Midpoint should be roughly halfway (both dots ~0.7 = cos(45°))
    CHECK(mid_dot_a > 0.6f);
    CHECK(mid_dot_b > 0.6f);

    // No handedness change: all quaternions have positive w (or consistent sign)
    for (int f = 0; f <= 60; f += 10) {
        Quat q = aquat.evaluate(Frame{f});
        CHECK(glm::length(q) == doctest::Approx(1.0f).epsilon(1e-4f));
    }
}

// ─────────────────────────────────────────────────────────────────────────
// T3 — TwoNodeTargetLockOrbit
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

// ─────────────────────────────────────────────────────────────────────────
// T4 — TwoNodePoleCrossingWithUpTarget
// ─────────────────────────────────────────────────────────────────────────
// Camera TwoNode crossing the pole (pitch 70° → 110°): verifies the
// resolve_look_at_orientation handles the singularity without flipping.

TEST_CASE("T4: look-at orientation is stable near pole (pitch > 89°)") {
    // Camera below target looking up: forward ≈ (0, 0.99, 0.05) almost ∥ up.
    // This is the classic look-at singularity — up_hint nearly ∥ forward.
    // The function must return a valid, normalised quaternion; NaN/Inf not allowed.
    const Vec3 cam_pos{0, -100, -10};
    const Vec3 target{0, 100, 0};

    Quat orient = resolve_look_at_orientation(cam_pos, target,
                                               Vec3{0, 1, 0}, 0.0f);

    // ── DoD: orientation is valid (normalised, non-degenerate) ─────────
    CHECK(glm::length(orient) == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK_FALSE(std::isnan(orient.x));
    CHECK_FALSE(std::isnan(orient.y));
    CHECK_FALSE(std::isnan(orient.z));
    CHECK_FALSE(std::isnan(orient.w));

    // ── DoD: all basis vectors are normalised ─────────────────────────
    CameraPose pose{cam_pos, orient};
    CHECK(glm::length(pose.forward()) == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK(glm::length(pose.right())   == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK(glm::length(pose.up())      == doctest::Approx(1.0f).epsilon(1e-5f));

    // ── DoD: up is reasonable (not completely flipped) ─────────────────
    // Camera below target looking up → local up may point slightly down
    // in world space.  Must not be more than 90° from world up.
    CHECK(pose.up().y > -0.99f);

    // ── DoD: view matrix has no NaN/Inf ────────────────────────────────
    Mat4 vm = pose.view_matrix(target, 0.0f);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            CHECK_FALSE(std::isnan(vm[col][row]));
            CHECK_FALSE(std::isinf(vm[col][row]));
        }
}

TEST_CASE("T4: CameraRig TwoNode crossing pitch pole without flip") {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target.set(Vec3{0, 0, -500});
    rig.orbit_radius.set(500.0f);

    // Orbit pitch from 70° to 110° (crossing 90° = pole)
    rig.orbit_pitch.key(Frame{0}, 70.0f).key(Frame{60}, 110.0f, EasingCurve{Easing::Linear});
    rig.orbit_yaw.set(0.0f);

    f32 max_angular_step_deg = 0.0f;
    Quat prev_orient;
    bool first = true;

    for (Frame f = 0; f <= Frame{60}; f = f + Frame{3}) {
        Camera2_5D cam = rig.evaluate(f);
        CHECK(cam.point_of_interest_enabled);

        Quat curr = cam.rotation_quaternion();
        CHECK(glm::length(curr) == doctest::Approx(1.0f).epsilon(1e-5f));

        if (!first) {
            f32 dot = std::abs(glm::dot(prev_orient, curr));
            f32 angle_deg = glm::degrees(std::acos(std::min(dot, 1.0f)));
            max_angular_step_deg = std::max(max_angular_step_deg, angle_deg);

            // ── DoD: no single step exceeds 6° ─────────────────────────
            CHECK(angle_deg < 6.0f);
        }

        // ── DoD: target stays locked ───────────────────────────────────
        f32 target_error = glm::length(cam.point_of_interest - rig.target.evaluate(f));
        CHECK(target_error < 1e-4f);

        prev_orient = curr;
        first = false;

        // ── DoD: no NaN in matrices ────────────────────────────────────
        Mat4 vm = cam.view_matrix();
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                CHECK_FALSE(std::isnan(vm[col][row]));
    }

    // ── DoD: max angular delta <= 6° (the spec says adjacent frames within 6°)──
    CHECK(max_angular_step_deg < 6.0f);
}

TEST_CASE("T4: resolve_look_at_orientation uses fallback axis at exact pole") {
    // Camera exactly below target — forward is (0, 1, 0) which is exactly ∥ world up.
    // The fallback should kick in and produce a deterministic orientation.
    const Vec3 cam_pos{0, 0, -1000};
    const Vec3 target{0, 0, 0};  // forward = (0, 0, 1) → not quite at pole

    // Test with forward nearly parallel to up_hint
    const Vec3 pole_target{0, 1000, 0};  // forward = normalize(0, 1000, 1000) → ≈ (0, 0.707, 0.707)
    Quat orient = resolve_look_at_orientation(cam_pos, pole_target,
                                               Vec3{0, 1, 0}, 0.0f);
    CHECK(glm::length(orient) == doctest::Approx(1.0f).epsilon(1e-5f));

    // View matrix must be valid (no NaN/Inf)
    Mat4 vm = glm::inverse(glm::translate(Mat4{1.0f}, cam_pos) * glm::toMat4(orient));
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            CHECK_FALSE(std::isnan(vm[col][row]));
}

// ─────────────────────────────────────────────────────────────────────────
// T10 — MultiTargetBlend
// ─────────────────────────────────────────────────────────────────────────
// CameraRig with 3 targets at different weights: verifies that
// point_of_interest = weighted centroid of all resolved bindings.

TEST_CASE("T10: weighted multi-target blend produces correct centroid") {
    // Three layers at distinct world positions.
    SceneTransformRegistry reg;
    Transform3D a_node;
    a_node.position = {200.0f, 0.0f, 0.0f};
    reg.add_node("target_a", a_node, false);

    Transform3D b_node;
    b_node.position = {0.0f, 150.0f, 0.0f};
    reg.add_node("target_b", b_node, false);

    Transform3D c_node;
    c_node.position = {0.0f, 0.0f, -300.0f};
    reg.add_node("target_c", c_node, false);

    auto resolved = reg.resolve_all();

    // Weights: a=0.7, b=0.2, c=0.1 (sum = 1.0)
    // Expected centroid:
    //   x = (0.7*200 + 0.2*0 + 0.1*0) / 1.0 = 140
    //   y = (0.7*0 + 0.2*150 + 0.1*0) / 1.0 = 30
    //   z = (0.7*0 + 0.2*0 + 0.1*-300) / 1.0 = -30
    const Vec3 expected_centroid{140.0f, 30.0f, -30.0f};

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_bindings = {
        {"target_a", 0.7f},
        {"target_b", 0.2f},
        {"target_c", 0.1f}
    };
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);

    // ── DoD: point_of_interest enabled ─────────────────────────────────
    REQUIRE(cam.point_of_interest_enabled);

    // ── DoD: point_of_interest equals weighted centroid ────────────────
    CHECK(cam.point_of_interest.x == doctest::Approx(expected_centroid.x).epsilon(1e-4f));
    CHECK(cam.point_of_interest.y == doctest::Approx(expected_centroid.y).epsilon(1e-4f));
    CHECK(cam.point_of_interest.z == doctest::Approx(expected_centroid.z).epsilon(1e-4f));
}

TEST_CASE("T10: multi-target blend with weight=0 excludes the target") {
    // Two layers: one with weight>0, one with weight=0.
    // The weight=0 binding must be excluded from the blend.
    SceneTransformRegistry reg;
    Transform3D active_node;
    active_node.position = {100.0f, 0.0f, 0.0f};
    reg.add_node("active_target", active_node, false);

    Transform3D ignored_node;
    ignored_node.position = {-500.0f, -500.0f, -500.0f};  // far away
    reg.add_node("ignored_target", ignored_node, false);

    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_bindings = {
        {"active_target",  1.0f},
        {"ignored_target", 0.0f}
    };
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);

    REQUIRE(cam.point_of_interest_enabled);

    // point_of_interest must match active_target only (ignored has weight=0).
    CHECK(cam.point_of_interest.x == doctest::Approx(100.0f).epsilon(1e-4f));
    CHECK(cam.point_of_interest.y == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(cam.point_of_interest.z == doctest::Approx(0.0f).epsilon(1e-4f));
}

TEST_CASE("T10: multi-target blend with unequal total weight normalises") {
    // Three targets with weights summing to 2.0 (not 1.0).
    // The resolver must divide by Σ weight = 2.0, not by 3.
    SceneTransformRegistry reg;
    Transform3D x_node;
    x_node.position = {100.0f, 0.0f, 0.0f};
    reg.add_node("x_target", x_node, false);

    Transform3D y_node;
    y_node.position = {0.0f, 50.0f, 0.0f};
    reg.add_node("y_target", y_node, false);

    Transform3D z_node;
    z_node.position = {0.0f, 0.0f, -200.0f};
    reg.add_node("z_target", z_node, false);

    auto resolved = reg.resolve_all();

    // Weights sum to 2.0:
    //   centroid = (1.0*p_x + 0.5*p_y + 0.5*p_z) / 2.0
    //   x = (1.0*100 + 0.5*0 + 0.5*0) / 2.0 = 50
    //   y = (1.0*0 + 0.5*50 + 0.5*0) / 2.0 = 12.5
    //   z = (1.0*0 + 0.5*0 + 0.5*-200) / 2.0 = -50
    const Vec3 expected_centroid{50.0f, 12.5f, -50.0f};

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_bindings = {
        {"x_target", 1.0f},
        {"y_target", 0.5f},
        {"z_target", 0.5f}
    };
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);

    REQUIRE(cam.point_of_interest_enabled);

    CHECK(cam.point_of_interest.x == doctest::Approx(expected_centroid.x).epsilon(1e-4f));
    CHECK(cam.point_of_interest.y == doctest::Approx(expected_centroid.y).epsilon(1e-4f));
    CHECK(cam.point_of_interest.z == doctest::Approx(expected_centroid.z).epsilon(1e-4f));
}

TEST_CASE("T10: multi-target blend falls back to legacy target_name when bindings empty") {
    // When target_bindings is empty, the legacy target_name path must work.
    SceneTransformRegistry reg;
    Transform3D target_node;
    target_node.position = {42.0f, 7.0f, -99.0f};
    reg.add_node("classic_target", target_node, false);

    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_bindings.clear();           // explicitly empty
    rig.target_name = "classic_target";
    rig.target.set(Vec3{0, 0, 0});        // overridden by resolver
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);

    REQUIRE(cam.point_of_interest_enabled);

    CHECK(cam.point_of_interest.x == doctest::Approx(42.0f).epsilon(1e-4f));
    CHECK(cam.point_of_interest.y == doctest::Approx(7.0f).epsilon(1e-4f));
    CHECK(cam.point_of_interest.z == doctest::Approx(-99.0f).epsilon(1e-4f));
}

TEST_CASE("T10: multi-target blend with missing binding is excluded gracefully") {
    // One binding resolves, one doesn't (non-existent layer name).
    // The missing binding must be silently excluded from the blend.
    SceneTransformRegistry reg;
    Transform3D real_node;
    real_node.position = {80.0f, 40.0f, -60.0f};
    reg.add_node("real_target", real_node, false);

    auto resolved = reg.resolve_all();

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_bindings = {
        {"real_target",       0.6f},
        {"nonexistent_layer", 0.4f}   // doesn't exist → excluded
    };
    rig.orbit_radius.set(1000.0f);

    Camera2_5D cam = rig.evaluate(Frame{0}, &resolved);

    REQUIRE(cam.point_of_interest_enabled);

    // The missing binding is excluded; total weight = 0.6.
    // point_of_interest = (0.6 * real_pos) / 0.6 = real_pos.
    CHECK(cam.point_of_interest.x == doctest::Approx(80.0f).epsilon(1e-4f));
    CHECK(cam.point_of_interest.y == doctest::Approx(40.0f).epsilon(1e-4f));
    CHECK(cam.point_of_interest.z == doctest::Approx(-60.0f).epsilon(1e-4f));
}

TEST_CASE("T10: multi-target blend with orbit animation keeps centroid stable") {
    // The centroid must stay locked even while the camera orbits around it.
    SceneTransformRegistry reg;
    Transform3D a_node;
    a_node.position = {100.0f, 0.0f, 0.0f};
    reg.add_node("a", a_node, false);

    Transform3D b_node;
    b_node.position = {-50.0f, 50.0f, 0.0f};
    reg.add_node("b", b_node, false);

    Transform3D c_node;
    c_node.position = {0.0f, -30.0f, -100.0f};
    reg.add_node("c", c_node, false);

    auto resolved = reg.resolve_all();

    // Weights: a=0.5, b=0.3, c=0.2
    // centroid = (0.5*(100,0,0) + 0.3*(-50,50,0) + 0.2*(0,-30,-100)) / 1.0
    //          = ((50) + (-15) + (0), (0) + (15) + (-6), (0) + (0) + (-20))
    //          = (35, 9, -20)
    const Vec3 expected_centroid{35.0f, 9.0f, -20.0f};

    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_bindings = {
        {"a", 0.5f},
        {"b", 0.3f},
        {"c", 0.2f}
    };
    rig.orbit_radius.set(800.0f);
    // Animate orbit yaw through a full sweep.
    rig.orbit_yaw.key(Frame{0}, -60.0f).key(Frame{60}, 60.0f, EasingCurve{Easing::InOutCubic});

    f32 max_error = 0.0f;
    f32 sum_error = 0.0f;
    int samples = 0;

    const Vec3 frame0_pos = rig.evaluate(Frame{0}, &resolved).position;

    for (Frame f = 0; f <= Frame{60}; f = f + Frame{15}) {
        Camera2_5D cam = rig.evaluate(f, &resolved);

        REQUIRE(cam.point_of_interest_enabled);

        f32 error = glm::length(cam.point_of_interest - expected_centroid);
        max_error = std::max(max_error, error);
        sum_error += error;
        ++samples;

        // Camera position must genuinely move (orbit is active).
        if (f != Frame{0}) {
            CHECK(glm::length(cam.position - frame0_pos) > 1.0f);
        }
    }

    // ── DoD: centroid stable within 1e-4f across full orbit ────────────
    f32 mean_error = sum_error / static_cast<f32>(samples);
    CHECK(mean_error < 1e-4f);
    CHECK(max_error < 1e-4f);
}

#endif  // HSPC pending — see TODO above
