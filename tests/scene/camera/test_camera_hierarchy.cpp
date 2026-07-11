#include <doctest/doctest.h>

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <cmath>

// PUBLIC camera hierarchy test — depends only on the public scene-hierarchy
// resolution surface (resolve_camera_hierarchy). No backend-internal headers
// (scene_internal.hpp, projection_utils.hpp, ...); internal coverage lives
// in tests/backends/software/utils/ and any future graph-internal companion
// tests.
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

// TICKET-007.h (CLOSED in this commit): pre-existing rot; "fast target swap"
// camera hierarchy resolution bug. Root cause: in resolve_camera_hierarchy,
// detail::world_anchor_point was being called with `from_mat4(target_result.
// world_matrix, ...)` as the first arg. from_mat4() decomposes world_matrix
// back into a Transform whose anchor field defaults to {0,0,0} (decomposition
// cannot recover the original anchor), so the POI always resolved to the
// LAYER's world origin (720, 30) instead of the ANCHOR's world point
// (position + anchor) = (520, 40). Fix: pass `layers[target_idx].transform`
// directly so the anchor survives. Numbers: position (720, 30, 0) +
// anchor (-200, 10, 0) -> POI (520, 40, 0).
TEST_CASE("Camera hierarchy: target with anchor resolves POI to anchor (not layer origin)") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("target_b", [](LayerBuilder& l) {
        l.position({720, 30, 0}).anchor({-200, 10, 0});
    });

    s.camera().enable()
     .position({0, 0, -1000})
     .target("target_b");

    auto scene = s.build();
    auto resolved = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());

    CHECK(resolved.camera.point_of_interest.x == doctest::Approx(520.0f).epsilon(0.0001f));
    CHECK(resolved.camera.point_of_interest.y == doctest::Approx(40.0f).epsilon(0.0001f));
    CHECK(resolved.camera.point_of_interest.z == doctest::Approx(0.0f).epsilon(0.0001f));
    CHECK(resolved.camera.point_of_interest_enabled);
}

