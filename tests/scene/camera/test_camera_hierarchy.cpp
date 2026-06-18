#include <doctest/doctest.h>

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <cmath>

#include "src/render_graph/pipeline/scene_internal.hpp"
using namespace chronon3d;


TEST_CASE("Camera hierarchy: target resolves through parent chain") {
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
    auto resolved = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());

    CHECK(resolved.camera.point_of_interest.x == doctest::Approx(250.0f).epsilon(0.0001f));
    CHECK(resolved.camera.point_of_interest.y == doctest::Approx(30.0f).epsilon(0.0001f));
    CHECK(resolved.camera.point_of_interest.z == doctest::Approx(0.0f).epsilon(0.0001f));
    CHECK(resolved.camera.point_of_interest_enabled);
}

TEST_CASE("Camera hierarchy: parent moves camera in world space") {
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
    auto resolved = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());

    CHECK(resolved.camera.position.x == doctest::Approx(100.0f).epsilon(0.0001f));
    CHECK(resolved.camera.position.y == doctest::Approx(200.0f).epsilon(0.0001f));
    CHECK(resolved.camera.position.z == doctest::Approx(-1000.0f).epsilon(0.0001f));
    CHECK(resolved.camera.rotation.x == doctest::Approx(5.0f).epsilon(0.0001f));
    CHECK(resolved.camera.rotation.y == doctest::Approx(10.0f).epsilon(0.0001f));
    CHECK(resolved.camera.rotation.z == doctest::Approx(15.0f).epsilon(0.0001f));
}

TEST_CASE("Camera hierarchy: parent rotation moves the camera around the origin") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.enable_3d().rotate({0, 90, 0});
    });

    s.camera().set({
        .enabled = true,
        .position = {0, 0, -1000},
        .rotation = {0.0f, 0.0f, 0.0f}
    });
    s.camera().parent("rig");

    auto scene = s.build();
    auto resolved = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());

    CHECK(std::abs(resolved.camera.position.x) == doctest::Approx(1000.0f).epsilon(0.01f));
    CHECK(std::abs(resolved.camera.position.y) < 0.5f);
    CHECK(std::abs(resolved.camera.position.z) < 0.5f);
}

TEST_CASE("Camera hierarchy: fast target swap is detected") {
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

    Camera2_5DRuntime swapped = scene.camera_2_5d();
    swapped.hierarchy_baked = false;
    swapped.point_of_interest_enabled = false;
    swapped.point_of_interest = {0.0f, 0.0f, 0.0f};
    swapped.target_name = std::pmr::string{"target_b", scene.resource()};

    auto resolved_b = resolve_camera_hierarchy(scene.layers(), scene.resource(), swapped);

    CHECK(resolved_b.camera.point_of_interest.x == doctest::Approx(520.0f).epsilon(0.0001f));
    CHECK(resolved_b.camera.point_of_interest.y == doctest::Approx(40.0f).epsilon(0.0001f));
    CHECK(chronon3d::graph::detail::camera_changed(resolved_b.camera, &resolved_a.camera, true));
}


