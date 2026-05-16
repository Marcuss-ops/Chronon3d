#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

TEST_CASE("Camera pose math is shared between Camera and Camera2_5D") {
    Camera cam3d;
    cam3d.transform.position = {10.0f, 20.0f, 30.0f};
    cam3d.set_rotation_euler({15.0f, 25.0f, 35.0f});

    Camera2_5D cam25d;
    cam25d.position = {10.0f, 20.0f, 30.0f};
    cam25d.set_rotation_euler({15.0f, 25.0f, 35.0f});

    const Mat4 view3d = cam3d.view_matrix();
    const Mat4 view25d = cam25d.view_matrix();

    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK(view3d[c][r] == doctest::Approx(view25d[c][r]).epsilon(0.0001f));
        }
    }
}

TEST_CASE("Camera tilt helpers update individual axes") {
    Camera cam;
    cam.set_tilt(10.0f);
    cam.add_pan(20.0f);
    cam.add_roll(-5.0f);

    const Vec3 euler = cam.rotation_euler();
    CHECK(euler.x == doctest::Approx(10.0f));
    CHECK(euler.y == doctest::Approx(20.0f));
    CHECK(euler.z == doctest::Approx(-5.0f));

    Camera2_5D cam25d;
    cam25d.set_tilt(10.0f);
    cam25d.add_pan(20.0f);
    cam25d.add_roll(-5.0f);

    CHECK(cam25d.rotation.x == doctest::Approx(10.0f));
    CHECK(cam25d.rotation.y == doctest::Approx(20.0f));
    CHECK(cam25d.rotation.z == doctest::Approx(-5.0f));
}

TEST_CASE("SceneBuilder exposes camera tilt helpers") {
    SceneBuilder s;
    s.camera().enable()
     .tilt(12.0f)
     .pan(-8.0f)
     .roll(3.5f);

    auto scene = s.build();
    const auto cam = scene.camera_2_5d();

    CHECK(cam.enabled);
    CHECK(cam.rotation.x == doctest::Approx(12.0f));
    CHECK(cam.rotation.y == doctest::Approx(-8.0f));
    CHECK(cam.rotation.z == doctest::Approx(3.5f));
}
