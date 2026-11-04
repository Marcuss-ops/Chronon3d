#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
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

TEST_CASE("combined physical camera projection has a numeric lens-shift golden") {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -1000.0f};
    camera.optics_mode = CameraOpticsMode::PhysicalLens;
    camera.lens = LensModel{85.0f, 2.8f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    camera.lens_shift = {0.025f, -0.01f};
    camera.near_plane = 100.0f;
    camera.far_plane = 1200.0f;
    camera.dof.enabled = true;
    camera.dof.focus_distance = 1100.0f;
    camera.dof.max_blur = 12.0f;

    const auto projected = camera_math::project_world_point(
        camera, Vec3{110.0f, 55.0f, 100.0f}, camera_math::Viewport2D{1920.0f, 1080.0f});

    // The physical gate maps the 36 mm width and 24 mm height independently
    // to the requested viewport. Camera-space depth is 1100; lens shift is
    // expressed as a viewport fraction.
    CHECK(projected.visible);
    CHECK(projected.depth == doctest::Approx(1100.0f));
    CHECK(projected.perspective_scale == doctest::Approx(3825.0f / 1100.0f).epsilon(1e-5));
    CHECK(projected.screen.x == doctest::Approx(1461.3334f).epsilon(1e-4));
    CHECK(projected.screen.y == doctest::Approx(337.95f).epsilon(1e-4));
}
