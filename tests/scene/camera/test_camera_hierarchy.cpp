#include <doctest/doctest.h>

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <cmath>

using namespace chronon3d;

TEST_CASE("Camera hierarchy: target resolves to world position through parent chain") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.position({200, 0, 0});
    });
    s.null_layer("target", [](LayerBuilder& l) {
        l.parent("rig").position({50, 30, 0});
    });

    s.camera().enable()
     .position({0, 0, -1000})
     .target("target");

    auto scene = s.build();
    auto resolved_layers = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());
    REQUIRE(resolved_layers.size() == 2);

    auto resolved_cam = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());
    CHECK(resolved_cam.camera.point_of_interest.x == doctest::Approx(250.0f));
    CHECK(resolved_cam.camera.point_of_interest.y == doctest::Approx(30.0f));
    CHECK(resolved_cam.camera.point_of_interest.z == doctest::Approx(0.0f));
}

TEST_CASE("Camera hierarchy: parent moves the camera without changing orientation") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.position({100, 200, 0});
    });

    s.camera().set({
        .enabled = true,
        .position = {0, 0, -1000},
        .rotation = {5.0f, 10.0f, 15.0f}
    });
    s.camera().parent("rig");

    auto scene = s.build();
    auto resolved_layers = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());
    REQUIRE(resolved_layers.size() == 1);

    auto resolved_cam = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());
    CHECK(resolved_cam.camera.position.x == doctest::Approx(100.0f));
    CHECK(resolved_cam.camera.position.y == doctest::Approx(200.0f));
    CHECK(resolved_cam.camera.position.z == doctest::Approx(-1000.0f));
    CHECK(resolved_cam.camera.rotation.x == doctest::Approx(5.0f));
    CHECK(resolved_cam.camera.rotation.y == doctest::Approx(10.0f));
    CHECK(resolved_cam.camera.rotation.z == doctest::Approx(15.0f));
}

TEST_CASE("Camera hierarchy: parent rotation moves the camera in 3D") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.enable_3d()
         .rotate({0, 90, 0});
    });

    s.camera().set({
        .enabled = true,
        .position = {0, 0, -1000},
        .rotation = {0.0f, 0.0f, 0.0f}
    });
    s.camera().parent("rig");

    auto scene = s.build();
    auto resolved_cam = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());

    CHECK(std::abs(resolved_cam.camera.position.x) == doctest::Approx(1000.0f).epsilon(0.01f));
    CHECK(resolved_cam.camera.position.y == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(std::abs(resolved_cam.camera.position.z) == doctest::Approx(0.0f).epsilon(0.01f));
}
