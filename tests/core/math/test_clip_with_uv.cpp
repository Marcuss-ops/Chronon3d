// ============================================================================
// test_clip_with_uv.cpp — Direct tests for the clip_with_uv helper
// (FASE 17: extracted from camera_projection_resolver.hpp into
// camera_projection_clip.hpp)
//
// Tests the Sutherland-Hodgman polygon clipping algorithm against a Z-plane
// with linearly interpolated UVs at the intersection point.  These cover
// the standalone contract — the polygon's geometry/UVs are constructed
// directly, no projector / resolver in the loop.
//
// Coverage (cat-2 freeze-compliant: deterministic, no threads, no time):
//   1. Polygon fully in front of near plane (no clipping)
//   2. Polygon fully behind near plane (entirely clipped, returns false)
//   3. Polygon straddling near plane (clipped to visible portion, UVs interp)
//   4. Far-plane clipping (clip_above=false): polygon straddling far plane
//   5. UV interpolation at intersection: linear `t = (z_thresh - a.z)/(b.z - a.z)`
//   6. Triangle input (count_in=3): at-least-3-vertices invariant
//   7. count_out == 0 -> returns false (safety)
// ============================================================================

#include <doctest/doctest.h>
#include <chronon3d/math/camera_projection_resolver.hpp>
#include <chronon3d/math/camera_projection_clip.hpp>
#include <cmath>
using namespace chronon3d;

// Helper to make a Vec3 — verbose to avoid type lookup ambiguity.
static Vec3 v3(f32 x, f32 y, f32 z) { return {x, y, z}; }
static Vec2 v2(f32 x, f32 y)       { return {x, y}; }

// =============================================================================
// Test 1: 4-vertex quad fully in front of near plane -> unchanged.
// =============================================================================
TEST_CASE("clip_with_uv: quad fully in front of near plane is unchanged") {
    Vec3 verts[4] = {
        v3(-1.0f, -1.0f, 5.0f),
        v3( 1.0f, -1.0f, 5.0f),
        v3( 1.0f,  1.0f, 5.0f),
        v3(-1.0f,  1.0f, 5.0f),
    };
    Vec2 uvs[4]   = {
        v2(0.0f, 0.0f), v2(1.0f, 0.0f),
        v2(1.0f, 1.0f), v2(0.0f, 1.0f),
    };
    Vec3 out_v[4];
    Vec2 out_u[4];
    int count_out = 0;

    // near plane at z=0.001, clip_above=true (keep z >= 0.001)
    const f32 z_thresh = camera_math::kNearClipZ;
    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 4, out_v, out_u, &count_out, z_thresh, true);

    CHECK(ok);
    CHECK(count_out == 4);

    // All verts preserved (in input winding order)
    for (int i = 0; i < 4; ++i) {
        CHECK(out_v[i].x == verts[i].x);
        CHECK(out_v[i].y == verts[i].y);
        CHECK(out_v[i].z == verts[i].z);
        CHECK(out_u[i].x == uvs[i].x);
        CHECK(out_u[i].y == uvs[i].y);
    }
}

// =============================================================================
// Test 2: Quad fully behind near plane -> fully clipped, returns false.
// =============================================================================
TEST_CASE("clip_with_uv: quad fully behind near plane is fully clipped") {
    Vec3 verts[4] = {
        v3(-1.0f, -1.0f, -1.0f),   // z < 0.001 -> behind near plane
        v3( 1.0f, -1.0f, -1.0f),
        v3( 1.0f,  1.0f, -1.0f),
        v3(-1.0f,  1.0f, -1.0f),
    };
    Vec2 uvs[4]   = {
        v2(0.0f, 0.0f), v2(1.0f, 0.0f),
        v2(1.0f, 1.0f), v2(0.0f, 1.0f),
    };
    Vec3 out_v[4];
    Vec2 out_u[4];
    int count_out = 0;

    const f32 z_thresh = camera_math::kNearClipZ;
    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 4, out_v, out_u, &count_out, z_thresh, true);

    CHECK_FALSE(ok);   // less than 3 verts remaining -> not visible
    CHECK(count_out < 3);
}

// =============================================================================
// Test 3: Quad straddling near plane -> clipped, UVs linearly interpolated.
// Verifies parametric interp `t = (z_thresh - a.z) / (b.z - a.z)`:
//   Top edge (TL->TR) goes from z=10 (TL) to z=0.0001 (TR) -- waits z=0.0001
//   is behind, so we use z=10 / z=0.0001 across a diagonal.  Use a different
//   shape: TL z=10, TR z=0.0001 (behind), BR z=0.0001, BL z=10.
//   Top edge crosses near plane (z=0.001) at t where 10 + t*(0.0001-10) = 0.001
//   t = (0.001 - 10) / (0.0001 - 10) = -9.999 / -9.9999 ~= 0.99990
// =============================================================================
TEST_CASE("clip_with_uv: straddling quad yields clipped polygon with interp UVs") {
    Vec3 verts[4] = {
        v3(-1.0f, -1.0f, 10.0f),    // TL: in front
        v3( 1.0f, -1.0f,  0.0001f), // TR: behind near plane (z=0.0001 < 0.001)
        v3( 1.0f,  1.0f,  0.0001f), // BR: behind
        v3(-1.0f,  1.0f, 10.0f),    // BL: in front
    };
    Vec2 uvs[4]   = {
        v2(0.0f, 0.0f), v2(1.0f, 0.0f),
        v2(1.0f, 1.0f), v2(0.0f, 1.0f),
    };
    Vec3 out_v[6];
    Vec2 out_u[6];
    int count_out = 0;

    const f32 z_thresh = camera_math::kNearClipZ;
    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 4, out_v, out_u, &count_out, z_thresh, true);

    CHECK(ok);
    CHECK(count_out == 4);   // 4 verts: TL + 2 intersections + BL (sides recursed)

    // All output Z values must be >= z_thresh (they're in the visible half).
    for (int i = 0; i < count_out; ++i) {
        CHECK(out_v[i].z >= z_thresh);
    }

    // UVs must be in [0, 1] (interpolated, not just input endpoints).
    for (int i = 0; i < count_out; ++i) {
        CHECK(out_u[i].x >= -0.01f);
        CHECK(out_u[i].x <= 1.01f);
        CHECK(out_u[i].y >= -0.01f);
        CHECK(out_u[i].y <= 1.01f);
    }

    // The two TL/BL endpoints (z=10) should be preserved in output.
    // (We don't assert exact indices because Sutherland-Hodgman ordering
    // depends on the input winding — just check they exist somewhere.)
    bool found_tl = false, found_bl = false;
    for (int i = 0; i < count_out; ++i) {
        if (out_v[i].x == -1.0f && out_v[i].y == -1.0f && out_v[i].z >= 9.99f) found_tl = true;
        if (out_v[i].x == -1.0f && out_v[i].y ==  1.0f && out_v[i].z >= 9.99f) found_bl = true;
    }
    CHECK(found_tl);
    CHECK(found_bl);
}

// =============================================================================
// Test 4: Far-plane clipping (clip_above=false).
// Quad straddles far_plane=10.0: TL/TR at z=5 (in front), BR/BL at z=15 (beyond).
// =============================================================================
TEST_CASE("clip_with_uv: far-plane clipping (clip_above=false) preserves near half") {
    Vec3 verts[4] = {
        v3(-1.0f, -1.0f,  5.0f),   // TL: in front of far_plane=10
        v3( 1.0f, -1.0f,  5.0f),   // TR: in front
        v3( 1.0f,  1.0f, 15.0f),   // BR: beyond far_plane (>10)
        v3(-1.0f,  1.0f, 15.0f),   // BL: beyond
    };
    Vec2 uvs[4]   = {
        v2(0.0f, 0.0f), v2(1.0f, 0.0f),
        v2(1.0f, 1.0f), v2(0.0f, 1.0f),
    };
    Vec3 out_v[6];
    Vec2 out_u[6];
    int count_out = 0;

    const f32 far_plane = 10.0f;
    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 4, out_v, out_u, &count_out, far_plane, /*clip_above=*/false);

    CHECK(ok);
    CHECK(count_out == 4);

    // All output Z values must be <= far_plane.
    for (int i = 0; i < count_out; ++i) {
        CHECK(out_v[i].z <= far_plane);
    }
    TL_BACK:
    // TL and TR (z=5) must be preserved in output.
    bool found_tl = false, found_tr = false;
    for (int i = 0; i < count_out; ++i) {
        if (out_v[i].x == -1.0f && out_v[i].y == -1.0f && out_v[i].z == 5.0f) found_tl = true;
        if (out_v[i].x ==  1.0f && out_v[i].y == -1.0f && out_v[i].z == 5.0f) found_tr = true;
    }
    CHECK(found_tl);
    CHECK(found_tr);
}

// =============================================================================
// Test 5: UV interpolation math is linear.
// Test is constructed to have a known intersection point so we can check
// the interpolation is exact.
// =============================================================================
TEST_CASE("clip_with_uv: UV at intersection point is linearly interpolated") {
    // Diagonal edge TL(-1,-1,z=10) -> BR(1,1,z=0) crosses near plane (z=0.001).
    // But edges go to next vertex in winding order.  Use a 4-quad:
    //   TL UV=(0,0) z=10
    //   TR UV=(1,0) z=0.0001 (behind near)
    //   BR UV=(1,1) z=10
    //   BL UV=(0,1) z=10
    // TL->TR crosses near (z=0.001).  Param: 10 + t*(0.0001-10) = 0.001
    //   t = (0.001 - 10) / (0.0001 - 10) = -9.999 / -9.9999 ≈ 0.99990
    // Interp UV at intersection: (0,0) + t*((1,0)-(0,0)) = (t, 0).
    Vec3 verts[4] = {
        v3(-1.0f, -1.0f, 10.0f),
        v3( 1.0f, -1.0f,  0.0001f),
        v3( 1.0f,  1.0f, 10.0f),
        v3(-1.0f,  1.0f, 10.0f),
    };
    Vec2 uvs[4]   = {
        v2(0.0f, 0.0f), v2(1.0f, 0.0f),
        v2(1.0f, 1.0f), v2(0.0f, 1.0f),
    };
    Vec3 out_v[6];
    Vec2 out_u[6];
    int count_out = 0;

    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 4, out_v, out_u, &count_out,
        camera_math::kNearClipZ, true);

    // Find the corner where y == -1 and z is near 0.001 -- this should be the TL->TR crossing.
    bool found_intersection = false;
    for (int i = 0; i < count_out; ++i) {
        if (std::abs(out_v[i].y - (-1.0f)) < 1e-4f &&
            std::abs(out_v[i].z - camera_math::kNearClipZ) < 1e-4f) {
            // Intersect has x near +0.9999 (very close to TR x=1) and y=-1.
            // UV x must be near 0.9999 (linear interp), UV y = 0.
            CHECK(std::abs(out_u[i].x - 0.9999f) < 1e-3f);
            CHECK(std::abs(out_u[i].y - 0.0f) < 1e-3f);
            found_intersection = true;
            // Also: position x should be linearly interp too:
            //   x = -1 + t*(1 - (-1)) = -1 + 0.9999*2 = 0.9998
            CHECK(std::abs(out_v[i].x - 0.9998f) < 1e-3f);
        }
    }
    CHECK(found_intersection);
}

// =============================================================================
// Test 6: Triangle input (count_in=3) — at-least-3 invariant.
// =============================================================================
TEST_CASE("clip_with_uv: triangle fully in front is preserved") {
    Vec3 verts[3] = {
        v3(0.0f, 0.0f, 5.0f),
        v3(2.0f, 0.0f, 5.0f),
        v3(1.0f, 2.0f, 5.0f),
    };
    Vec2 uvs[3] = { v2(0,0), v2(1,0), v2(0.5f, 1.0f) };
    Vec3 out_v[4];
    Vec2 out_u[4];
    int count_out = 0;

    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 3, out_v, out_u, &count_out,
        camera_math::kNearClipZ, true);

    CHECK(ok);
    CHECK(count_out == 3);
}

// =============================================================================
// Test 7: count_in < 3 returns false immediately (safety).
// =============================================================================
TEST_CASE("clip_with_uv: count_in < 3 returns false") {
    Vec3 verts[2] = { v3(0, 0, 5), v3(1, 0, 5) };
    Vec2 uvs[2]   = { v2(0, 0), v2(1, 0) };
    Vec3 out_v[2];
    Vec2 out_u[2];
    int count_out = 99;   // sentinel: should be reset to 0

    bool ok = chronon3d::clip_with_uv(
        verts, uvs, 2, out_v, out_u, &count_out,
        camera_math::kNearClipZ, true);

    CHECK_FALSE(ok);
    CHECK(count_out == 0);
}
