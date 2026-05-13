#include <doctest/doctest.h>
#include <chronon3d/scene/scene_builder.hpp>
#include <chronon3d/scene/layer_hierarchy.hpp>

using namespace chronon3d;

TEST_CASE("Layer hierarchy: parent position affects child") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.position({100, 0, 0});
    });

    s.layer("child", [](LayerBuilder& l) {
        l.parent("rig")
         .position({50, 0, 0});
    });

    auto scene = s.build();
    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    const auto& child = resolved[1];

    CHECK(child.world_transform.position.x == doctest::Approx(150.0f));
    CHECK(child.world_transform.position.y == doctest::Approx(0.0f));
}

TEST_CASE("Layer hierarchy: parent opacity multiplies child opacity") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.opacity(0.5f);
    });

    s.layer("child", [](LayerBuilder& l) {
        l.parent("rig")
         .opacity(0.5f);
    });

    auto scene = s.build();
    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    CHECK(resolved[1].world_transform.opacity == doctest::Approx(0.25f));
}

TEST_CASE("Layer hierarchy: parent chain accumulates position") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("A", [](LayerBuilder& l) {
        l.position({10, 0, 0});
    });

    s.null_layer("B", [](LayerBuilder& l) {
        l.parent("A")
         .position({20, 0, 0});
    });

    s.layer("C", [](LayerBuilder& l) {
        l.parent("B")
         .position({30, 0, 0});
    });

    auto scene = s.build();
    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    CHECK(resolved[2].world_transform.position.x == doctest::Approx(60.0f));
}

TEST_CASE("Layer hierarchy: missing parent falls back to local transform") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.layer("child", [](LayerBuilder& l) {
        l.parent("missing")
         .position({30, 0, 0});
    });

    auto scene = s.build();
    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    CHECK(resolved[0].parent_missing);
    CHECK(resolved[0].world_transform.position.x == doctest::Approx(30.0f));
}

TEST_CASE("Layer hierarchy: self parent cycle is detected") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.layer("A", [](LayerBuilder& l) {
        l.parent("A")
         .position({10, 0, 0});
    });

    auto scene = s.build();
    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    CHECK(resolved[0].cycle_detected);
    CHECK(resolved[0].world_transform.position.x == doctest::Approx(10.0f));
}

TEST_CASE("Layer hierarchy: parent rotation affects child position") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    // Parent at origin, rotated 90 deg around Z
    s.null_layer("parent", [](LayerBuilder& l) {
        l.position({0, 0, 0})
         .rotate({0, 0, 90});
    });

    // Child offset by 100 on X locally.
    // Since parent is rotated 90 deg (X becomes Y), 
    // child world position should be around {0, 100, 0}.
    s.layer("child", [](LayerBuilder& l) {
        l.parent("parent")
         .position({100, 0, 0});
    });

    auto scene = s.build();
    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    CHECK(resolved[1].world_transform.position.x == doctest::Approx(0.0f));
    CHECK(resolved[1].world_transform.position.y == doctest::Approx(100.0f));
}

TEST_CASE("Layer hierarchy: camera inherits parent position") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.position({100, 200, 0});
    });

    s.camera_2_5d({
        .enabled = true,
        .position = {0, 0, -1000}
    });
    s.camera_parent("rig");

    auto scene = s.build();
    ResolvedCamera resolved_cam;
    resolve_layer_hierarchy(scene.layers(), 0, scene.resource(), &scene.camera_2_5d(), &resolved_cam);

    CHECK(resolved_cam.camera.position.x == doctest::Approx(100.0f));
    CHECK(resolved_cam.camera.position.y == doctest::Approx(200.0f));
    CHECK(resolved_cam.camera.position.z == doctest::Approx(-1000.0f));
}
