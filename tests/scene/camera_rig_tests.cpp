#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_rig_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/transform/transform_resolver.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

using namespace chronon3d;

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
    auto far = rig.evaluate(0);

    rig.dolly.set(200.0f);
    auto near = rig.evaluate(0);

    CHECK(glm::length(near.position - rig.target.evaluate(0)) <
          glm::length(far.position - rig.target.evaluate(0)));
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
