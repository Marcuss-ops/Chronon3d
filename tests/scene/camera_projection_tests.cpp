#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <cmath>
using namespace chronon3d;


TEST_CASE("Camera Projection logic check") {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -1000.0f};
    camera.zoom = 1000.0f;

    Viewport viewport{1920.0f, 1080.0f};

    SUBCASE("Target projects to center") {
        ScreenPoint sp = project_world_to_screen(Vec3{0, 0, 0}, camera, viewport);
        CHECK(!sp.behind_camera);
        CHECK(sp.position.x == doctest::Approx(960.0f));
        CHECK(sp.position.y == doctest::Approx(540.0f));
    }

    SUBCASE("Point behind camera detection") {
        ScreenPoint sp = project_world_to_screen(Vec3{0, 0, -1200.0f}, camera, viewport);
        CHECK(sp.behind_camera);
    }

    SUBCASE("Coincident point returns finite, behind_camera=true") {
        // Guard: when the queried world point is numerically coincident with the
        // camera position, downstream projection can divide by zero and emit
        // +-Inf / NaN. The guard short-circuits before the divide and emits a
        // safe ScreenPoint{position=(0,0), depth=0, behind_camera=true}.
        // Locks in the contract added in src/scene/camera/camera_projection.cpp.
        ScreenPoint sp = project_world_to_screen(camera.position, camera, viewport);
        CHECK(sp.behind_camera);
        CHECK(std::isfinite(sp.position.x));
        CHECK(std::isfinite(sp.position.y));
        CHECK(std::isfinite(sp.depth));
        CHECK_FALSE(std::isnan(sp.position.x));
        CHECK_FALSE(std::isnan(sp.position.y));
        CHECK_FALSE(std::isnan(sp.depth));
    }

    SUBCASE("FOV updates projection scaling") {
        camera.optics_mode = CameraOpticsMode::FieldOfView;
        camera.fov_deg = 90.0f; // wider FOV -> projects to smaller screen space offset

        ScreenPoint sp1 = project_world_to_screen(Vec3{100.0f, 0, 0}, camera, viewport);
        
        camera.fov_deg = 30.0f; // narrower FOV -> projects to larger screen space offset
        ScreenPoint sp2 = project_world_to_screen(Vec3{100.0f, 0, 0}, camera, viewport);

        CHECK(std::abs(sp2.position.x - 960.0f) > std::abs(sp1.position.x - 960.0f));
    }
}
