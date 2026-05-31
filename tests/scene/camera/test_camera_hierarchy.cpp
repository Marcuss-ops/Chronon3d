#include <doctest/doctest.h>

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <cmath>
#include "src/render_graph/pipeline/scene_internal.hpp"

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

TEST_CASE("Camera hierarchy: camera change detection tracks target swaps and fast motion") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.position({200, 0, 0});
    });
    s.null_layer("target_a", [](LayerBuilder& l) {
        l.parent("rig").position({50, 30, 0});
    });
    s.null_layer("target_b", [](LayerBuilder& l) {
        l.parent("rig").position({320, 40, 0});
    });

    s.camera().enable()
     .position({0, 0, -1000})
     .parent("rig")
     .target("target_a");

    auto scene = s.build();
    auto resolved_a = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());
    CHECK(resolved_a.camera.point_of_interest.x == doctest::Approx(250.0f));
    CHECK(resolved_a.camera.point_of_interest.y == doctest::Approx(30.0f));

    Camera2_5DRuntime target_swapped = scene.camera_2_5d();
    target_swapped.hierarchy_baked = false;
    target_swapped.point_of_interest = {0.0f, 0.0f, 0.0f};
    target_swapped.point_of_interest_enabled = false;
    target_swapped.target_name = std::pmr::string{"target_b", scene.resource()};
    auto resolved_b = resolve_camera_hierarchy(scene.layers(), scene.resource(), target_swapped);
    CHECK(resolved_b.camera.point_of_interest.x == doctest::Approx(520.0f));
    CHECK(resolved_b.camera.point_of_interest.y == doctest::Approx(40.0f));
    CHECK(chronon3d::graph::detail::camera_changed(resolved_b.camera, &resolved_a.camera, true));

    auto fast_moved = resolved_a.camera;
    fast_moved.position.x += 640.0f;
    CHECK(chronon3d::graph::detail::camera_changed(fast_moved, &resolved_a.camera, true));
}
