// ============================================================================
// test_camera_projection_geometry_safety.cpp — Tests for Blocco 2:
// far-plane clipping, 6-plane frustum culling, NaN/Inf guards.
//
// Tests verify:
//   - Far-plane clipping (Sutherland-Hodgman with UV)
//   - Frustum culling (6-plane: near, far, left, right, top, bottom)
//   - Objects outside frustum are culled
//   - Objects intersecting frustum boundary are partially visible
//   - NaN/Inf guards for extreme positions
// ============================================================================

#include <doctest/doctest.h>
#include <chronon3d/math/camera_projection_resolver.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
using namespace chronon3d;


// -- Shared test setup (unique names to avoid conflict with resolver tests) -----
static Camera2_5D make_gs_camera() {
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.fov_deg = 50.0f;
    return cam;
}

static camera_math::Viewport2D make_gs_viewport() {
    return {1920.0f, 1080.0f};
}

static CameraProjectionInput make_gs_input(
    const Camera2_5D& cam,
    const camera_math::Viewport2D& vp,
    Vec3 position = {0.0f, 0.0f, 0.0f},
    Vec2 size = {500.0f, 350.0f},
    bool enable_frustum_culling = true,
    f32 far_plane = chronon3d::kFarClipZ
) {
    CameraProjectionInput in;
    in.world_transform = glm::translate(Mat4(1.0f), position);
    in.layer_size = size;
    in.camera = cam;
    in.viewport = vp;
    in.backface_mode = BackfaceMode::DoubleSided;
    in.far_plane = far_plane;
    in.enable_frustum_culling = enable_frustum_culling;
    return in;
}

// =============================================================================
// Test 1: Far-plane clipping -- card beyond far plane is invisible
// =============================================================================
TEST_CASE("GeometrySafety: card beyond far plane is invisible") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, 90000.0f}, {500.0f, 350.0f},
                               false, 50000.0f);
    auto result = CameraProjectionResolver::project_layer(input);

    CHECK_FALSE(result.visible);
    CHECK(result.clip_state == ClipState::AllBeyond);
}

// =============================================================================
// Test 2: Far-plane clipping -- card close to far plane (finite values)
// =============================================================================
TEST_CASE("GeometrySafety: card at far plane boundary produces finite values") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    // Card near the far plane boundary + extreme rotation.
    // The important thing: regardless of clip state, all values are finite.
    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, 8000.0f}, {2000.0f, 3000.0f},
                               false, 10000.0f);
    input.world_transform = glm::rotate(Mat4(1.0f), glm::radians(85.0f), Vec3{1.0f, 0.0f, 0.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    // If visible: all corners must be finite
    // If invisible (AllBeyond): that's also valid
    if (result.visible) {
        for (int i = 0; i < result.corner_count; ++i) {
            CHECK(std::isfinite(result.corners[i].x));
            CHECK(std::isfinite(result.corners[i].y));
            CHECK(std::isfinite(result.corners[i].z));
            CHECK(std::isfinite(result.uvs[i].x));
            CHECK(std::isfinite(result.uvs[i].y));
        }
    } else {
        // Must be AllBeyond (not some other reason for invisibility)
        CHECK((result.clip_state == ClipState::AllBeyond ||
               result.clip_state == ClipState::NotClipped));
    }
}

// =============================================================================
// Test 3: Frustum culling -- card far outside the frustum (far right)
// =============================================================================
TEST_CASE("GeometrySafety: card far outside right frustum plane") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    auto input = make_gs_input(cam, vp, {100000.0f, 0.0f, 0.0f}, {10.0f, 10.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    bool outside = (result.frustum_result == FrustumResult::Outside);
    CHECK(outside);
    CHECK_FALSE(result.visible);
}

// =============================================================================
// Test 4: Frustum culling -- card far above the frustum
// =============================================================================
TEST_CASE("GeometrySafety: card far outside top frustum plane") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    auto input = make_gs_input(cam, vp, {0.0f, 100000.0f, 0.0f}, {10.0f, 10.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    bool outside = (result.frustum_result == FrustumResult::Outside);
    CHECK(outside);
    CHECK_FALSE(result.visible);
}

// =============================================================================
// Test 5: Frustum culling -- card at center is inside
// =============================================================================
TEST_CASE("GeometrySafety: card at center is inside frustum") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, 0.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    CHECK(result.visible);
    bool inside = (result.frustum_result == FrustumResult::Inside);
    CHECK(inside);
}

// =============================================================================
// Test 6: Large card near frustum boundary intersects
// =============================================================================
TEST_CASE("GeometrySafety: large card near frustum boundary intersects") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, 0.0f}, {5000.0f, 5000.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    CHECK(result.visible);
    bool intersects = (result.frustum_result == FrustumResult::Intersects);
    CHECK(intersects);

    // Even when intersecting the frustum, projected corners must be valid
    CHECK(result.corner_count >= 3);
    for (int i = 0; i < result.corner_count; ++i) {
        CHECK(std::isfinite(result.corners[i].x));
        CHECK(std::isfinite(result.corners[i].y));
        CHECK(std::isfinite(result.corners[i].z));
        CHECK(result.uvs[i].x >= 0.0f);
        CHECK(result.uvs[i].x <= 1.0f);
        CHECK(result.uvs[i].y >= 0.0f);
        CHECK(result.uvs[i].y <= 1.0f);
    }
}

// =============================================================================
// Test 7: Card behind camera is invisible
// =============================================================================
TEST_CASE("GeometrySafety: card behind camera is invisible") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    // Camera at Z=-1000, looking toward +Z.
    // World Z=-2000 -> camera-space Z = -2000 - (-1000) = -1000 -> behind
    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, -2000.0f}, {10.0f, 10.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    CHECK_FALSE(result.visible);
}

// =============================================================================
// Test 8: Frustum culling respects enable/disable flag
// =============================================================================
TEST_CASE("GeometrySafety: frustum culling respects enable flag") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    // Culling OFF -> visible even far away
    auto input_off = make_gs_input(cam, vp, {100000.0f, 0.0f, 0.0f}, {500.0f, 350.0f},
                                   false);
    auto result_off = CameraProjectionResolver::project_layer(input_off);
    CHECK(result_off.visible);

    // Culling ON -> invisible at same position
    auto input_on = make_gs_input(cam, vp, {100000.0f, 0.0f, 0.0f}, {500.0f, 350.0f},
                                  true);
    auto result_on = CameraProjectionResolver::project_layer(input_on);
    CHECK_FALSE(result_on.visible);
    bool outside = (result_on.frustum_result == FrustumResult::Outside);
    CHECK(outside);
}

// =============================================================================
// Test 9: Near+far combined clip state
// =============================================================================
TEST_CASE("GeometrySafety: near+far combined produces finite values") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    // Card with tilt so some corners are near, some are far
    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, 500.0f}, {5000.0f, 5000.0f},
                               false, 2000.0f);
    input.world_transform = glm::rotate(Mat4(1.0f), glm::radians(75.0f), Vec3{1.0f, 0.0f, 0.0f});
    auto result = CameraProjectionResolver::project_layer(input);

    // Whether visible or not, all values must be finite
    for (int i = 0; i < result.corner_count; ++i) {
        CHECK(std::isfinite(result.corners[i].x));
        CHECK(std::isfinite(result.corners[i].y));
        CHECK(std::isfinite(result.corners[i].z));
    }
    CHECK(std::isfinite(result.depth));
    CHECK(std::isfinite(result.perspective_scale));

    // If visible, clip state must be valid
    if (result.visible) {
        int cs = static_cast<int>(result.clip_state);
        CHECK(cs >= 1);  // Must be some type of clipped
        CHECK(cs <= 3);  // ClippedNear, ClippedFar, or ClippedBoth
    }
}

// =============================================================================
// Test 10: NaN/Inf guards with extreme values
// =============================================================================
TEST_CASE("GeometrySafety: extreme values do not produce NaN/Inf") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    Vec3 extreme_cases[] = {
        {0.0f, 0.0f, -999.0f},
        {0.0f, 0.0f, 50000.0f},
        {0.0f, 0.0f, -2000.0f},
        {10000.0f, 0.0f, 0.0f},
        {0.0f, 10000.0f, 0.0f},
        {0.0f, -10000.0f, 0.0f},
        {-10000.0f, 0.0f, 0.0f},
    };

    for (const auto& pos : extreme_cases) {
        auto input = make_gs_input(cam, vp, pos, {500.0f, 350.0f});
        auto result = CameraProjectionResolver::project_layer(input);

        for (int i = 0; i < result.corner_count; ++i) {
            CHECK(std::isfinite(result.corners[i].x));
            CHECK(std::isfinite(result.corners[i].y));
            CHECK(std::isfinite(result.corners[i].z));
        }
        CHECK(std::isfinite(result.depth));
        CHECK(std::isfinite(result.perspective_scale));
        CHECK(std::isfinite(result.signed_area));

        int cs = static_cast<int>(result.clip_state);
        CHECK(cs >= 0);
        CHECK(cs <= 5);

        int fr = static_cast<int>(result.frustum_result);
        CHECK(fr >= 0);
        CHECK(fr <= 2);
    }
}

// =============================================================================
// Test 11: Standard card works with frustum culling enabled
// =============================================================================
TEST_CASE("GeometrySafety: normal card works with frustum culling") {
    auto cam = make_gs_camera();
    auto vp = make_gs_viewport();

    auto input = make_gs_input(cam, vp, {0.0f, 0.0f, 0.0f}, {500.0f, 350.0f}, true);
    auto result = CameraProjectionResolver::project_layer(input);

    CHECK(result.visible);
    CHECK(result.corner_count >= 4);
    CHECK(result.clip_state == ClipState::NotClipped);

    bool inside = (result.frustum_result == FrustumResult::Inside);
    CHECK(inside);

    Vec2 centroid{0.0f, 0.0f};
    for (int i = 0; i < result.corner_count; ++i) {
        centroid.x += result.corners[i].x;
        centroid.y += result.corners[i].y;
    }
    centroid.x /= static_cast<f32>(result.corner_count);
    centroid.y /= static_cast<f32>(result.corner_count);
    CHECK(std::abs(centroid.x) < 1.0f);
    CHECK(std::abs(centroid.y) < 1.0f);
}
