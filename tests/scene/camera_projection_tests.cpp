#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_projection.hpp>
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

    SUBCASE("FOV updates projection scaling") {
        camera.projection_mode = Camera2_5DProjectionMode::Fov;
        camera.fov_deg = 90.0f; // wider FOV -> projects to smaller screen space offset

        ScreenPoint sp1 = project_world_to_screen(Vec3{100.0f, 0, 0}, camera, viewport);
        
        camera.fov_deg = 30.0f; // narrower FOV -> projects to larger screen space offset
        ScreenPoint sp2 = project_world_to_screen(Vec3{100.0f, 0, 0}, camera, viewport);

        CHECK(std::abs(sp2.position.x - 960.0f) > std::abs(sp1.position.x - 960.0f));
    }
}
