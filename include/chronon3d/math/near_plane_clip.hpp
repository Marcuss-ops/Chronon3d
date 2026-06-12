#pragma once

// ============================================================================
// near_plane_clip.hpp — Near-Plane Polygon Clipping for 2.5D Camera
//
/// @file    near_plane_clip.hpp
/// @brief   Sutherland-Hodgman polygon clipping against the camera near plane.
///
/// When a 3D layer (card) is rotated and some vertices cross behind the camera,
/// the projection can produce NaN, infinite values, or extreme perspective
/// distortions.  This module clips the layer's camera-space polygon against
/// the near plane (z = kNearZ) so that only the visible portion is projected.
///
/// Contract:
///   - Points with z >= kNearZ are "inside" (visible)
///   - Points with z < kNearZ are "outside" (behind camera plane)
///   - The clipped polygon is re-tessellated and projected stably
///   - If fewer than 3 points remain after clipping, the polygon is invisible
///   - NaN / inf are never produced
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <array>
#include <algorithm>
#include <limits>

namespace chronon3d::camera_math {

// ── Constants ────────────────────────────────────────────────────────────
// The near-plane Z threshold.  Points with z < kNearClipZ are clipped.
inline constexpr f32 kNearClipZ = 1e-3f;

// ── Clipped polygon result ──────────────────────────────────────────────
struct ClippedPolygon3D {
    std::array<Vec3, 8> points{};   // clipped vertices (max 8 for quad→hex)
    int   count{0};                  // number of valid vertices
    bool  visible{false};            // true when count >= 3 and polygon is finite
    bool  was_clipped{false};        // true if any vertex was actually clipped
};

// ── Intersection: edge endpoint on the near plane ────────────────────────
// Linearly interpolate between `a` (outside) and `b` (inside) to find
// the point where z == kNearClipZ.
inline Vec3 intersect_near_plane(const Vec3& a, const Vec3& b, f32 near_z = kNearClipZ) {
    // t ∈ [0, 1] where a.z + t * (b.z - a.z) = near_z
    const f32 dz = b.z - a.z;
    if (std::abs(dz) < 1e-12f) {
        // Degenerate edge — return a point just in front of the plane
        Vec3 mid = (a + b) * 0.5f;
        mid.z = near_z;
        return mid;
    }
    const f32 t = (near_z - a.z) / dz;
    return a + (b - a) * t;
}

// ── Sutherland-Hodgman clip against near plane ───────────────────────────
//
// Clips a convex polygon (given in camera space) against the near plane
// z = kNearClipZ.  The polygon is assumed to have vertices in CCW or CW
// order (any consistent winding).
//
// @param input      Array of input vertices (first `count` are valid).
// @param count      Number of valid vertices in `input` (max 8).
// @param near_z     Near-plane Z threshold (default kNearClipZ).
// @return           ClippedPolygon3D with clipped vertices.
template <size_t N>
inline ClippedPolygon3D clip_polygon_against_near_plane(
    const std::array<Vec3, N>& input,
    int count,
    f32 near_z = kNearClipZ
) {
    ClippedPolygon3D out;

    if (count < 3) {
        return out;  // not a valid polygon
    }

    // Work buffer — start with the input
    std::array<Vec3, 8> buffer0{};
    std::array<Vec3, 8> buffer1{};
    int buf_count = count;
    for (int i = 0; i < count && i < static_cast<int>(N); ++i) {
        buffer0[i] = input[i];
    }

    auto* src = &buffer0;
    auto* dst = &buffer1;
    int src_count = buf_count;
    int dst_count = 0;

    bool any_clipped = false;

    // Sutherland-Hodgman: for each edge (curr → next), decide output
    for (int i = 0; i < src_count; ++i) {
        const Vec3& curr = (*src)[i];
        const Vec3& next = (*src)[(i + 1) % src_count];

        const bool curr_inside = curr.z >= near_z;
        const bool next_inside = next.z >= near_z;

        if (curr_inside && next_inside) {
            // Both inside → emit next
            (*dst)[dst_count++] = next;
        } else if (curr_inside && !next_inside) {
            // Leaving the near plane → emit intersection
            (*dst)[dst_count++] = intersect_near_plane(curr, next, near_z);
            any_clipped = true;
        } else if (!curr_inside && next_inside) {
            // Entering the near plane → emit intersection + next
            (*dst)[dst_count++] = intersect_near_plane(curr, next, near_z);
            (*dst)[dst_count++] = next;
            any_clipped = true;
        }
        // else: both outside → emit nothing
    }

    // If nothing was clipped, return the original polygon
    if (!any_clipped) {
        out.count = src_count;
        for (int i = 0; i < src_count; ++i) {
            out.points[i] = (*src)[i];
        }
        out.visible = (src_count >= 3);
        out.was_clipped = false;
        return out;
    }

    // Copy result, validate
    out.count = dst_count;
    out.was_clipped = true;

    if (out.count < 3) {
        out.visible = false;
        return out;
    }

    // Validate all points are finite
    bool all_finite = true;
    for (int i = 0; i < out.count; ++i) {
        const Vec3& p = (*dst)[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            all_finite = false;
            break;
        }
    }

    if (!all_finite) {
        out.visible = false;
        return out;
    }

    out.visible = true;
    for (int i = 0; i < out.count; ++i) {
        out.points[i] = (*dst)[i];
    }

    return out;
}

// ── Convenience: clip a quad (4 camera-space corners) ────────────────────
inline ClippedPolygon3D clip_quad_against_near_plane(
    const std::array<Vec3, 4>& camera_quad,
    f32 near_z = kNearClipZ
) {
    return clip_polygon_against_near_plane(camera_quad, 4, near_z);
}

} // namespace chronon3d::camera_math
