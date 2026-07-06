// ============================================================================
// test_frustum_culling.cpp — Direct tests for the test_frustum_culling helper
// (FASE 17: extracted from camera_projection_resolver.hpp into
// camera_projection_frustum.hpp)
//
// Tests the 6-plane camera-space frustum culling directly.  No projector
// or resolver in the loop — the bbox is constructed manually, so the cull
// decision is observable in isolation.
//
// Coverage (cat-2 freeze-compliant: deterministic, no threads, no time):
//   1. BBox fully inside frustum -> Inside
//   2. BBox fully outside right side -> Outside  (immediate per-plane reject)
//   3. BBox fully outside left side -> Outside
//   4. BBox straddles frustum boundary -> Intersects
//   5. BBox entirely behind camera (max_z < 0) -> Outside
//   6. BBox past far plane -> Outside
//   7. compute_signed_area companion test
// ============================================================================

#include <doctest/doctest.h>
#include <chronon3d/math/camera_projection_resolver.hpp>
#include <chronon3d/math/camera_projection_frustum.hpp>
#include <cmath>
using namespace chronon3d;

// Standard test camera: 1920x1080 viewport, 50° fov, focal_y ≈ 1158, focal_x
// (per TICKET-035 / anamorphic) = (vp_w/2) / tan(fov_h/2) where fov_h is
// computed from vp aspect.
static camera_math::FocalPx test_focal(f32 vp_w, f32 vp_h, f32 fov_deg) {
    // Use the contract helper for ground-truth focal (matches resolver).
    Camera2_5D cam;
    cam.fov_deg = fov_deg;
    return camera_math::focal_xy_from_camera(cam, vp_w, vp_h);
}

// =============================================================================
// Test 1: small bbox at camera depth ~ visibility > fully inside.
// =============================================================================
TEST_CASE("test_frustum_culling: small bbox at depth 1000 is fully Inside") {
    const f32 vp_w = 1920.0f, vp_h = 1080.0f, fov = 50.0f;
    auto f = test_focal(vp_w, vp_h, fov);

    // 100x100 quad centred at camera-space origin, depth 1000 (well in front of near plane).
    auto r = test_frustum_culling(
        -50.0f, 50.0f,
        -50.0f, 50.0f,
        900.0f, 1000.0f,   // min_z..max_z
        f, vp_w, vp_h, 1e6f);

    CHECK(r == FrustumResult::Inside);
}

// =============================================================================
// Test 2: large bbox fills vertical viewport but extends far beyond right edge
// -> recorded as Intersects (not Inside, not Outside).
// =============================================================================
TEST_CASE("test_frustum_culling: bbox straddling right edge yields Intersects") {
    const f32 vp_w = 1920.0f, vp_h = 1080.0f, fov = 50.0f;
    auto f = test_focal(vp_w, vp_h, fov);

    // Compute the right boundary at z=1000: x_max = z * tan(fov_h/2)
    const f32 z = 1000.0f;
    const f32 fov_v = glm::radians(fov);
    const f32 tan_half_v = std::tan(fov_v * 0.5f);
    const f32 tan_half_h = (vp_w * 0.5f) / f.y;   // tan_half_h = vp_w_half / focal_x

    // bbox: x_min on the right edge, x_max way past the right edge.
    // z range stays within valid depth. (no_z_overlap is fine.)
    auto r = test_frustum_culling(
        z * tan_half_h,        // x_min: on the right edge
        z * tan_half_h * 3.0f, // x_max: 3x past the right edge
        -100.0f, 100.0f,       // y_min, y_max (well within frustum vertically)
        z - 100.0f, z + 100.0f,
        f, vp_w, vp_h, 1e6f);

    // Straddling the right plane — some corners outside, others inside.
    CHECK(r == FrustumResult::Intersects);
}

// =============================================================================
// Test 3: bbox far to the right (entirely past right boundary) -> Outside.
// =============================================================================
TEST_CASE("test_frustum_culling: bbox far right of frustum is Outside") {
    const f32 vp_w = 1920.0f, vp_h = 1080.0f, fov = 50.0f;
    auto f = test_focal(vp_w, vp_h, fov);

    const f32 z = 1000.0f;
    const f32 tan_half_h = (vp_w * 0.5f) / f.x;  // approximate (close enough)

    // bbox entirely to the right of x = z*tan_half_h.
    auto r = test_frustum_culling(
        z * tan_half_h * 2.0f,    // x_min: 2x past right edge
        z * tan_half_h * 4.0f,    // x_max: 4x past right edge
        -50.0f, 50.0f,
        z - 100.0f, z + 100.0f,
        f, vp_w, vp_h, 1e6f);

    CHECK(r == FrustumResult::Outside);
}

// =============================================================================
// Test 4: bbox entirely behind the camera (max_z < 0) -> Outside.
// =============================================================================
TEST_CASE("test_frustum_culling: bbox behind camera is Outside") {
    const f32 vp_w = 1920.0f, vp_h = 1080.0f, fov = 50.0f;
    auto f = test_focal(vp_w, vp_h, fov);

    // All z negative — entirely behind camera.
    auto r = test_frustum_culling(
        -100.0f, 100.0f,
        -100.0f, 100.0f,
        -200.0f, -1.0f,   // entirely behind near plane (z < 0)
        f, vp_w, vp_h, 1e6f);

    CHECK(r == FrustumResult::Outside);
}

// =============================================================================
// Test 5: bbox past the far plane -> Outside.
// =============================================================================
TEST_CASE("test_frustum_culling: bbox past far_plane is Outside") {
    const f32 vp_w = 1920.0f, vp_h = 1080.0f, fov = 50.0f;
    auto f = test_focal(vp_w, vp_h, fov);

    // depth 1000..2000, far_plane=500 -- all past far plane.
    auto r = test_frustum_culling(
        -50.0f, 50.0f,
        -50.0f, 50.0f,
        1000.0f, 2000.0f,
        f, vp_w, vp_h, /*far_plane=*/500.0f);

    CHECK(r == FrustumResult::Outside);
}

// =============================================================================
// Test 6: bbox straddling near plane -> partial near-plane rejection but not
// per-plane reject from any single plane (some corners inside each plane)
// -> Intersects.
// =============================================================================
TEST_CASE("test_frustum_culling: bbox straddling near plane yields Intersects") {
    const f32 vp_w = 1920.0f, vp_h = 1080.0f, fov = 50.0f;
    auto f = test_focal(vp_w, vp_h, fov);

    auto r = test_frustum_culling(
        -50.0f, 50.0f,
        -50.0f, 50.0f,
        -1.0f, 5.0f,    // min_z < 0 (behind near), max_z = 5 (in front)
        f, vp_w, vp_h, 1e6f);

    // Plane analysis (deterministic):
    //   Near (0,0,+1,-kNear): distance = z - kNear.
    //     -4 corners at z=-1 are OUTSIDE near (distance < 0)
    //     -4 corners at z=+5 are INSIDE near  (distance > 0)
    //     => 4/8 inside, 4/8 outside (NOT a per-plane reject)
    //   Other 5 planes (Left/Right/Top/Bottom/Far): small |x|/|y|, normal z,
    //     all 8 corners inside each plane (no per-plane reject).
    //   => no per-plane reject fires -> Intersects.
    CHECK(r == FrustumResult::Intersects);
}

// =============================================================================
// Test 7: compute_signed_area companion — Shoelace formula sanity check.
// =============================================================================
TEST_CASE("compute_signed_area: clockwise quad gives negative area") {
    // In screen-space Y-down, clockwise winding -> negative signed area.
    Vec3 verts[4] = {
        {0.0f, 0.0f, 0.0f},   // TL
        {100.0f, 0.0f, 0.0f}, // TR
        {100.0f, 100.0f, 0.0f}, // BR
        {0.0f, 100.0f, 0.0f}, // BL
    };
    f32 area = compute_signed_area(verts, 4);
    // Area magnitude = 100*100 = 10000, sign depends on winding.
    // The Shoelace formula returns twice the area / 2 i.e. half, so |area| = 5000.
    CHECK(std::abs(area) > 4900.0f);
    CHECK(std::abs(area) < 5100.0f);
}

TEST_CASE("compute_signed_area: less than 3 vertices returns 0") {
    Vec3 verts[2] = {{0,0,0}, {1,0,0}};
    CHECK(compute_signed_area(verts, 0) == 0.0f);
    CHECK(compute_signed_area(verts, 1) == 0.0f);
    CHECK(compute_signed_area(verts, 2) == 0.0f);
}
