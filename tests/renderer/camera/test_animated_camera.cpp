#include <doctest/doctest.h>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>

using namespace chronon3d;

namespace {

bool vec3_near(const Vec3& a, const Vec3& b, float eps = 0.01f) {
    return std::abs(a.x - b.x) <= eps &&
           std::abs(a.y - b.y) <= eps &&
           std::abs(a.z - b.z) <= eps;
}

} // namespace

// =============================================================================
// AnimatedCamera2_5D — basic evaluation
// =============================================================================

TEST_CASE("AnimatedCamera2_5D: defaults produce expected Camera2_5D") {
    AnimatedCamera2_5D cam;
    Camera2_5D result = cam.evaluate(0);

    CHECK(result.enabled);
    CHECK(vec3_near(result.position, Vec3{0.0f, 0.0f, -1000.0f}));
    CHECK(vec3_near(result.rotation, Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(result.zoom == doctest::Approx(1000.0f));
    CHECK(result.fov_deg == doctest::Approx(50.0f));
}

TEST_CASE("AnimatedCamera2_5D: static values hold across frames") {
    AnimatedCamera2_5D cam;
    cam.position.set(Vec3{100.0f, 200.0f, -800.0f});
    cam.zoom.set(1200.0f);
    cam.fov_deg.set(35.0f);

    Camera2_5D r0 = cam.evaluate(0);
    Camera2_5D r60 = cam.evaluate(60);
    Camera2_5D r120 = cam.evaluate(120);

    CHECK(vec3_near(r0.position, Vec3{100.0f, 200.0f, -800.0f}));
    CHECK(vec3_near(r60.position, Vec3{100.0f, 200.0f, -800.0f}));
    CHECK(vec3_near(r120.position, Vec3{100.0f, 200.0f, -800.0f}));
    CHECK(r60.zoom == doctest::Approx(1200.0f));
    CHECK(r60.fov_deg == doctest::Approx(35.0f));
}

TEST_CASE("AnimatedCamera2_5D: linear keyframe interpolation works") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(0, Vec3{0.0f, 0.0f, -1000.0f})
        .key(60, Vec3{0.0f, 0.0f, -500.0f});

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r30 = cam.evaluate(30);
    Camera2_5D r60 = cam.evaluate(60);

    CHECK(r0.position.z  == doctest::Approx(-1000.0f));
    CHECK(r30.position.z == doctest::Approx(-750.0f));
    CHECK(r60.position.z == doctest::Approx(-500.0f));
}

TEST_CASE("AnimatedCamera2_5D: eased keyframe with OutCubic") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(0, Vec3{0.0f, 0.0f, -1000.0f}, EasingCurve{Easing::OutCubic})
        .key(60, Vec3{0.0f, 0.0f, -500.0f});

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r15 = cam.evaluate(15); // t=0.25, OutCubic ~ 0.578
    Camera2_5D r30 = cam.evaluate(30); // t=0.50, OutCubic ~ 0.875
    Camera2_5D r45 = cam.evaluate(45); // t=0.75, OutCubic ~ 0.984
    Camera2_5D r60 = cam.evaluate(60);

    // All values increase (move from -1000 toward -500)
    CHECK(r0.position.z  < r15.position.z);
    CHECK(r15.position.z < r30.position.z);
    CHECK(r30.position.z < r45.position.z);
    CHECK(r45.position.z < r60.position.z);

    // OutCubic eases out: fast start, deceleration toward end.
    // The per-frame delta should shrink as we approach the target.
    f32 d0_15  = r15.position.z - r0.position.z;
    f32 d15_30 = r30.position.z - r15.position.z;
    f32 d30_45 = r45.position.z - r30.position.z;
    f32 d45_60 = r60.position.z - r45.position.z;
    CHECK(d0_15  > d15_30);
    CHECK(d15_30 > d30_45);
    CHECK(d30_45 > d45_60);

    // Final frame is exactly the end value
    CHECK(r60.position.z == doctest::Approx(-500.0f));
}

TEST_CASE("AnimatedCamera2_5D: hold mode clamps at edges") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(30, Vec3{0.0f, 0.0f, -800.0f})
        .key(60, Vec3{0.0f, 0.0f, -400.0f});

    // Hold mode (default): before first key, return first key value
    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r30 = cam.evaluate(30);
    Camera2_5D r90 = cam.evaluate(90); // after last key

    CHECK(vec3_near(r0.position, Vec3{0.0f, 0.0f, -800.0f}));
    CHECK(vec3_near(r30.position, Vec3{0.0f, 0.0f, -800.0f}));
    CHECK(vec3_near(r90.position, Vec3{0.0f, 0.0f, -400.0f}));
}

TEST_CASE("AnimatedCamera2_5D: DOF fields are animatable") {
    AnimatedCamera2_5D cam;
    cam.focus_z
        .key(0, 100.0f)
        .key(30, 500.0f);

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r30 = cam.evaluate(30);

    CHECK(r0.dof.focus_z == doctest::Approx(100.0f));
    CHECK(r30.dof.focus_z == doctest::Approx(500.0f));
}

TEST_CASE("AnimatedCamera2_5D: is_animated detects keyframed properties") {
    AnimatedCamera2_5D cam;

    CHECK_FALSE(cam.is_animated());

    cam.position.key(0, Vec3{0.0f, 0.0f, -500.0f});
    CHECK(cam.is_animated());
}

TEST_CASE("AnimatedCamera2_5D: point_of_interest keyframes work") {
    AnimatedCamera2_5D cam;
    cam.point_of_interest_enabled = true;
    cam.point_of_interest
        .key(0, Vec3{0.0f, 0.0f, 0.0f})
        .key(60, Vec3{100.0f, 50.0f, 200.0f});

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r60 = cam.evaluate(60);

    CHECK(r0.point_of_interest_enabled);
    CHECK(vec3_near(r0.point_of_interest, Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(vec3_near(r60.point_of_interest, Vec3{100.0f, 50.0f, 200.0f}));
}

// =============================================================================
// CameraRig presets
// =============================================================================

TEST_CASE("CameraRig: hero_push_in produces expected motion") {
    auto cam = camera_rig::hero_push_in();

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r90 = cam.evaluate(90);

    // Start position is behind
    CHECK(r0.position.z == doctest::Approx(-1200.0f));
    // End position is closer
    CHECK(r90.position.z == doctest::Approx(-750.0f));
    // Tilt changes
    CHECK(r0.rotation.x == doctest::Approx(-4.0f));
    CHECK(r90.rotation.x == doctest::Approx(2.0f));
}

TEST_CASE("CameraRig: dolly_zoom moves position and zoom in opposite directions") {
    auto cam = camera_rig::dolly_zoom();

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r90 = cam.evaluate(90);

    // Position moves IN (z becomes less negative = closer)
    CHECK(r0.position.z  == doctest::Approx(-1200.0f));
    CHECK(r90.position.z == doctest::Approx(-600.0f));
    // Zoom moves OUT (zoom decreases)
    CHECK(r0.zoom  == doctest::Approx(1200.0f));
    CHECK(r90.zoom == doctest::Approx(600.0f));
}

TEST_CASE("CameraRig: focus_pull animates only focus_z") {
    auto cam = camera_rig::focus_pull({
        .from_focus_z = 0.0f,
        .to_focus_z   = 1000.0f,
    });

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r60 = cam.evaluate(60);

    // Position unchanged
    CHECK(r0.position.z  == doctest::Approx(-1000.0f));
    CHECK(r60.position.z == doctest::Approx(-1000.0f));
    // Focus shifts
    CHECK(r0.dof.focus_z  == doctest::Approx(0.0f));
    CHECK(r60.dof.focus_z == doctest::Approx(1000.0f));
}

TEST_CASE("CameraRig: parallax_pan sweeps horizontally") {
    auto cam = camera_rig::parallax_pan();

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r90 = cam.evaluate(90);

    CHECK(r0.position.x  == doctest::Approx(-180.0f));
    CHECK(r90.position.x == doctest::Approx(180.0f));
    // Z should be constant
    CHECK(r0.position.z  == doctest::Approx(-1000.0f));
    CHECK(r90.position.z == doctest::Approx(-1000.0f));
}

TEST_CASE("CameraRig: low_angle_reveal rises and rotates") {
    auto cam = camera_rig::low_angle_reveal();

    Camera2_5D r0  = cam.evaluate(0);
    Camera2_5D r90 = cam.evaluate(90);

    // Starts low
    CHECK(r0.position.y == doctest::Approx(-180.0f));
    // Rises
    CHECK(r90.position.y == doctest::Approx(40.0f));
    // Tilt changes from looking up to level
    CHECK(r0.rotation.x == doctest::Approx(25.0f));
    CHECK(r90.rotation.x == doctest::Approx(0.0f));
}

TEST_CASE("CameraRig: orbit_yaw sweeps around target with POI enabled") {
    auto cam = camera_rig::orbit_yaw();

    Camera2_5D r0   = cam.evaluate(0);
    Camera2_5D r120 = cam.evaluate(120);

    // POI should be enabled and fixed on target
    CHECK(r0.point_of_interest_enabled);
    CHECK(r120.point_of_interest_enabled);
    CHECK(vec3_near(r0.point_of_interest, Vec3{0.0f, 0.0f, 0.0f}));

    // Position should orbit (x changes with angle)
    CHECK(r0.position.x < 0.0f);   // starts at -25 degrees → sin(-25) < 0
    CHECK(r120.position.x > 0.0f); // ends at +25 degrees → sin(25) > 0
}

TEST_CASE("CameraRig: subtle_float oscillates around base position") {
    auto cam = camera_rig::subtle_float();

    Camera2_5D r0   = cam.evaluate(0);
    Camera2_5D r150 = cam.evaluate(150);
    Camera2_5D r300 = cam.evaluate(300);

    // Values should vary slightly around the base position
    CHECK(std::abs(r0.position.x)   < 20.0f);
    CHECK(std::abs(r150.position.x) < 20.0f);
    CHECK(std::abs(r300.position.x) < 20.0f);
    // Not always the same value
    CHECK((r0.position.x != r150.position.x || r150.position.x != r300.position.x));
}

// =============================================================================
// SceneBuilder integration
// =============================================================================

TEST_CASE("SceneBuilder: animated_camera() sets camera from AnimatedCamera2_5D") {
    auto rig = camera_rig::hero_push_in();

    SceneBuilder builder(960, 540);
    builder.animated_camera(rig);

    auto scene = builder.build();
    const auto& cam = scene.camera_2_5d();

    // Frame 0 of hero_push_in: position at (-1200), tilt -4°, yaw 0°
    CHECK(cam.enabled);
    CHECK(cam.position.z == doctest::Approx(-1200.0f));
    CHECK(cam.rotation.x == doctest::Approx(-4.0f));
    CHECK(cam.rotation.y == doctest::Approx(0.0f));
}

TEST_CASE("SceneBuilder: animated_camera() evaluates at current frame") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(0,   Vec3{0.0f, 0.0f, -1000.0f})
        .key(100, Vec3{0.0f, 0.0f, -200.0f});

    // Frame 0
    SceneBuilder b0(960, 540);
    b0.animated_camera(cam);
    auto s0 = b0.build();
    CHECK(s0.camera_2_5d().position.z == doctest::Approx(-1000.0f));
}

TEST_CASE("SceneBuilder: animated_camera() returns *this for chaining") {
    auto rig = camera_rig::parallax_pan();

    auto scene = SceneBuilder(960, 540)
        .animated_camera(rig)
        .rect("bg", {.size = {960, 540}, .color = Color::black(), .pos = {0, 0, 0}})
        .build();

    CHECK(scene.camera_2_5d().enabled);
    CHECK(scene.camera_2_5d().position.x == doctest::Approx(-180.0f));
    CHECK(scene.nodes().size() == 1);
    CHECK(scene.nodes()[0].name == "bg");
}

TEST_CASE("SceneBuilder: camera().set_animated() works through CameraApi") {
    auto rig = camera_rig::dolly_zoom();

    SceneBuilder builder(960, 540);
    builder.camera().set_animated(rig);

    auto scene = builder.build();
    const auto& cam = scene.camera_2_5d();

    CHECK(cam.position.z == doctest::Approx(-1200.0f));
    CHECK(cam.zoom == doctest::Approx(1200.0f));
    CHECK(cam.point_of_interest_enabled);
}

TEST_CASE("SceneBuilder: camera().set_animated() chains with other CameraApi methods") {
    auto rig = camera_rig::focus_pull();

    SceneBuilder builder(960, 540);
    builder.camera()
        .set_animated(rig)
        .zoom(800.0f)  // override zoom after animated camera
        .look_at({50.0f, 0.0f, 0.0f});

    auto scene = builder.build();
    const auto& cam = scene.camera_2_5d();

    // Animated camera set DOF values
    CHECK(cam.dof.focus_z == doctest::Approx(0.0f));
    // zoom was overridden after set_animated
    CHECK(cam.zoom == doctest::Approx(800.0f));
    // look_at overrides POI
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.point_of_interest.x == doctest::Approx(50.0f));
}

TEST_CASE("SceneBuilder: animated_camera() with FrameContext evaluates at ctx frame") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(0,  Vec3{0.0f, 0.0f, -1000.0f})
        .key(50, Vec3{0.0f, 0.0f, -500.0f});

    FrameContext ctx;
    ctx.frame.frame.frame = 50;
    ctx.frame.frame.width = 960;
    ctx.frame.frame.height = 540;

    SceneBuilder builder(ctx);
    builder.animated_camera(cam);

    auto scene = builder.build();
    CHECK(scene.camera_2_5d().position.z == doctest::Approx(-500.0f));
}
