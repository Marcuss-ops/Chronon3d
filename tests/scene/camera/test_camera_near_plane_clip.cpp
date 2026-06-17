// ============================================================================
// test_camera_near_plane_clip.cpp
//
/// @brief   Tests for near-plane polygon clipping in 2.5D camera projection.
///
/// Verifies that:
///   - Layers fully behind the camera are invisible
///   - Layers crossing the near plane produce stable (NaN-free) output
///   - The Sutherland-Hodgman clipping works correctly for edge cases
///   - perspective_scale and depth are finite for clipped layers
// ============================================================================

#include <doctest/doctest.h>
#include <chronon3d/math/near_plane_clip.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

using namespace chronon3d;

// ────────────────────────────────────────────────────────────────────────────
// Sutherland-Hodgman clipping unit tests
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("Near plane clipping: fully front quad passes through unchanged") {
    std::array<Vec3, 4> quad = {{
        {-100, -50, 500},
        { 100, -50, 500},
        { 100,  50, 500},
        {-100,  50, 500},
    }};

    auto clipped = camera_math::clip_quad_against_near_plane(quad);

    CHECK(clipped.visible);
    CHECK_FALSE(clipped.was_clipped);
    CHECK(clipped.count == 4);
}

TEST_CASE("Near plane clipping: fully behind quad is invisible") {
    std::array<Vec3, 4> quad = {{
        {-100, -50, -100},
        { 100, -50, -100},
        { 100,  50, -100},
        {-100,  50, -100},
    }};

    auto clipped = camera_math::clip_quad_against_near_plane(quad);

    CHECK_FALSE(clipped.visible);
}

TEST_CASE("Near plane clipping: quad crossing near plane produces 4-6 vertices") {
    // Quad straddling the near plane at z=0.001
    // Two corners in front (z=500), two behind (z=-100)
    std::array<Vec3, 4> quad = {{
        {-100, -50, 500},    // front-left
        { 100, -50, -100},   // back-right
        { 100,  50, -100},   // back-left
        {-100,  50, 500},    // front-right
    }};

    auto clipped = camera_math::clip_quad_against_near_plane(quad);

    CHECK(clipped.visible);
    CHECK(clipped.was_clipped);
    CHECK(clipped.count >= 4);
    CHECK(clipped.count <= 6);

    // All clipped points must be finite and in front of near plane
    // Epsilon 1e-5 is used because intersect_near_plane computes in float32
    // with z~0.001, and FP round-trip through the intersection formula can
    // produce errors ~7e-6.
    for (int i = 0; i < clipped.count; ++i) {
        CHECK(std::isfinite(clipped.points[i].x));
        CHECK(std::isfinite(clipped.points[i].y));
        CHECK(std::isfinite(clipped.points[i].z));
        CHECK(clipped.points[i].z >= camera_math::kNearClipZ - 1e-5f);
    }
}

TEST_CASE("Near plane clipping: three corners behind, one in front") {
    std::array<Vec3, 4> quad = {{
        {-100, -50, 500},    // front
        { 100, -50, -100},   // behind
        { 100,  50, -100},   // behind
        {-100,  50, -100},   // behind
    }};

    auto clipped = camera_math::clip_quad_against_near_plane(quad);

    CHECK(clipped.visible);
    CHECK(clipped.was_clipped);
    CHECK(clipped.count >= 3);
    CHECK(clipped.count <= 5);

    for (int i = 0; i < clipped.count; ++i) {
        CHECK(std::isfinite(clipped.points[i].z));
    }
}

TEST_CASE("Near plane clipping: edge exactly on near plane") {
    std::array<Vec3, 4> quad = {{
        {-100, -50, 0.0f},   // on the plane
        { 100, -50, 0.0f},   // on the plane
        { 100,  50, 500},    // in front
        {-100,  50, 500},    // in front
    }};

    auto clipped = camera_math::clip_quad_against_near_plane(quad);

    CHECK(clipped.visible);
    CHECK(clipped.count >= 4);
}

// ────────────────────────────────────────────────────────────────────────────
// Integration tests: project_layer_2_5d with near-plane crossing
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("Camera near clipping: fully behind layer is invisible") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;

    Transform t;
    t.position = {0, 0, -1200};

    auto projected = project_layer_2_5d(t, cam, 1920, 1080);
    CHECK_FALSE(projected.visible);
}

TEST_CASE("Camera near clipping: layer crossing near plane does not explode") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    cam.point_of_interest = {0, 0, 0};
    cam.point_of_interest_enabled = true;

    // Layer positioned so that its centroid is just in front of the camera,
    // but with extreme rotation: some corners pass behind the camera.
    Transform t;
    t.position = {0, 0, -999.9f};
    t.rotation = glm::quat(glm::radians(Vec3{0, 80, 0}));

    auto projected = project_layer_2_5d(t, cam, 1920, 1080);

    // The layer crosses the near plane (kNearClipZ = 0.001).  Clipped corners
    // landing at the near plane get perspective_scale = focal / kNearClipZ
    // ≈ 1000 / 0.001 = 1,000,000, which is finite but large.  The upper bound
    // is set wide enough to accommodate this worst case.
    CHECK(projected.visible);
    CHECK(std::isfinite(projected.perspective_scale));
    CHECK(projected.perspective_scale < 2'000'000.0f);
    CHECK(std::isfinite(projected.depth));
    CHECK(projected.depth > 0.0f);
}

TEST_CASE("Camera near clipping: layer rotated 45 degrees crossing near plane") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    cam.point_of_interest = {0, 0, 0};
    cam.point_of_interest_enabled = true;

    Transform t;
    t.position = {0, 0, -995.0f};  // centroid at depth 5, very close
    t.rotation = glm::quat(glm::radians(Vec3{30, 45, 0}));  // compound rotation

    auto projected = project_layer_2_5d(t, cam, 1920, 1080);

    CHECK(projected.visible);
    CHECK(std::isfinite(projected.perspective_scale));
    CHECK(projected.perspective_scale < 10000.0f);
    CHECK(std::isfinite(projected.depth));
}

TEST_CASE("Camera near clipping: layer far behind but with rotation") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    cam.point_of_interest = {0, 0, 0};
    cam.point_of_interest_enabled = true;

    Transform t;
    t.position = {0, 0, -2000};  // far behind
    t.rotation = glm::quat(glm::radians(Vec3{0, 30, 0}));

    auto projected = project_layer_2_5d(t, cam, 1920, 1080);

    // All corners should be behind → invisible
    CHECK_FALSE(projected.visible);
}

TEST_CASE("Camera near clipping: corner-case layer at exactly the plane") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    cam.point_of_interest = {0, 0, 0};
    cam.point_of_interest_enabled = true;

    Transform t;
    t.position = {0, 0, -1000};  // centroid ON the camera plane

    auto projected = project_layer_2_5d(t, cam, 1920, 1080);

    // Should be invisible (centroid is on/behind the near plane)
    CHECK_FALSE(projected.visible);
}

TEST_CASE("Camera near clipping: normal layer still works correctly") {
    // Verify that the fast path (no clipping) is unchanged for normal layers
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000;
    cam.point_of_interest = {0, 0, 0};
    cam.point_of_interest_enabled = true;

    Transform t;
    t.position = {0, 0, 0};  // well in front, no rotation
    t.scale = {1, 1, 1};

    auto projected = project_layer_2_5d(t, cam, 1920, 1080);

    CHECK(projected.visible);
    CHECK(projected.depth == doctest::Approx(1000.0f).epsilon(1.0f));
    CHECK(projected.perspective_scale == doctest::Approx(1.0f).epsilon(0.01f));
    CHECK(std::isfinite(projected.projection_matrix[0][0]));
}
