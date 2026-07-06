#pragma once

// ============================================================================
// camera_projection_frustum.hpp — Frustum culling + signed area helpers
//
// @file    camera_projection_frustum.hpp
// @brief   6-plane frustum culling + 2D polygon signed area computation.
//
// Extracted from camera_projection_resolver.hpp (FASE 17) to keep the
// primary resolver focused on project_layer() itself.  All functions
// are inline and live in the same namespace chronon3d as the parent.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <cstdint>

namespace chronon3d {

// -- Frustum culling result ----------------------------------------------------
// Defined here (post-fix #2 migration, FASE 17+): frustum.hpp is the only
// helper that returns this enum.  Standalone-consumers of frustum.hpp
// (only camera_projection_resolver.hpp for now) get the enum + the
// helper in one include.
enum class FrustumResult : uint8_t {
    Inside        = 0,   // Layer is fully inside the view frustum
    Intersects    = 1,   // Layer intersects frustum boundary (partially visible)
    Outside       = 2,   // Layer is fully outside the view frustum -> cull
};

// ============================================================================
// CameraProjectionResolver::test_frustum_culling
//
// Tests the camera-space bounding box against the 6 frustum planes.
// The frustum is defined by the FOV and viewport aspect ratio.
// Returns Inside / Intersects / Outside.
// TICKET-035: per-axis focal for anamorphic / Stretch frustum planes.
// ============================================================================
inline FrustumResult test_frustum_culling(
    f32 min_x, f32 max_x,
    f32 min_y, f32 max_y,
    f32 min_z, f32 max_z,
    camera_math::FocalPx focal_xy, f32 vp_width, f32 vp_height,
    f32 far_plane)
{
    const f32 tan_half_fov_v = (vp_height * 0.5f) / focal_xy.y;
    const f32 tan_half_fov_h = (vp_width  * 0.5f) / focal_xy.x;

    const Vec3 corners[8] = {
        {min_x, min_y, min_z}, {max_x, min_y, min_z},
        {max_x, max_y, min_z}, {min_x, max_y, min_z},
        {min_x, min_y, max_z}, {max_x, min_y, max_z},
        {max_x, max_y, max_z}, {min_x, max_y, max_z},
    };

    // 6 frustum planes in camera-space (Y-up, camera looks toward +Z):
    //   Near:  z >= kNearClipZ  ->  (0, 0, 1, -kNearClipZ)
    //   Far:   z <= far_plane   ->  (0, 0, -1, far_plane)
    //   Left:  x >= -z * tan_half_fov_h  ->  (1, 0, tan_half_fov_h, 0)
    //   Right: x <=  z * tan_half_fov_h  ->  (-1, 0, tan_half_fov_h, 0)
    //   Top:   y <=  z * tan_half_fov_v  ->  (0, -1, tan_half_fov_v, 0)
    //   Bottom: y >= -z * tan_half_fov_v  ->  (0, 1, tan_half_fov_v, 0)

    struct Plane { f32 nx, ny, nz, d; };
    const Plane planes[6] = {
        { 0.0f,  0.0f,  1.0f, -camera_math::kNearClipZ},  // near
        { 0.0f,  0.0f, -1.0f,  far_plane},                 // far (uses input param)
        { 1.0f,  0.0f,  tan_half_fov_h,  0.0f},            // left
        {-1.0f,  0.0f,  tan_half_fov_h,  0.0f},            // right
        { 0.0f, -1.0f,  tan_half_fov_v,  0.0f},            // top
        { 0.0f,  1.0f,  tan_half_fov_v,  0.0f},            // bottom
    };

    bool any_inside = false;
    bool all_inside = true;

    for (const auto& plane : planes) {
        int inside_count = 0;
        for (int i = 0; i < 8; ++i) {
            const f32 dist = corners[i].x * plane.nx
                           + corners[i].y * plane.ny
                           + corners[i].z * plane.nz
                           + plane.d;
            if (dist >= 0.0f) {
                inside_count++;
            }
        }

        if (inside_count == 0) {
            return FrustumResult::Outside;
        }

        // For corners behind the camera (z < 0), some planes are undefined.
        // We handle this by checking if the bbox intersects z > 0 region.
        if (inside_count < 8) {
            all_inside = false;
        }
        if (inside_count > 0) {
            any_inside = true;
        }
    }

    // Special case: if the bbox is entirely behind the camera (z < 0 for all corners)
    if (max_z < 0.0f) {
        return FrustumResult::Outside;
    }

    if (all_inside) return FrustumResult::Inside;
    if (any_inside) return FrustumResult::Intersects;
    return FrustumResult::Outside;
}

// ============================================================================
// CameraProjectionResolver::compute_signed_area
//
// Compute 2D signed area of a polygon (Shoelace formula).
// Positive winding = counterclockwise in screen Y-down convention.
// Used for diagnostics (cull hint, orientation); not for backface detection.
// ============================================================================
inline f32 compute_signed_area(const Vec3* vertices, int count) {
    if (count < 3) return 0.0f;
    f32 area = 0.0f;
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        area += vertices[i].x * vertices[j].y;
        area -= vertices[j].x * vertices[i].y;
    }
    return area * 0.5f;
}

} // namespace chronon3d
