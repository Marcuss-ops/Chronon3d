#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_path_sampler.hpp>

using namespace chronon3d;

TEST_CASE("Camera Path Sampler test suite") {
    CameraRig rig;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);
    rig.orbit_yaw.set(0.0f);

    TransformResolverResult resolved;
    Viewport viewport{1920.0f, 1080.0f};

    CameraPathReport report = sample_camera_path(rig, resolved, viewport, 0, 30, 10);
    
    CHECK(report.samples.size() == 4);
    CHECK(report.passed);
}
