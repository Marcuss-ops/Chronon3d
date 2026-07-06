#pragma once

// ============================================================================
// camera_projection_clip.hpp — Z-plane polygon clipping with UV interpolation
//
// @file    camera_projection_clip.hpp
// @brief   Sutherland-Hodgman polygon clipping against a Z-plane.
//          Vertex positions and UVs are linearly interpolated at the
//          plane-crossing parametric position.
//
// Extracted from camera_projection_resolver.hpp (FASE 17) to keep the
// primary resolver focused on project_layer() itself.  All functions
// are inline and live in the same namespace chronon3d as the parent.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <utility>

namespace chronon3d {

// ============================================================================
// CameraProjectionResolver::clip_with_uv
//
// Clip a convex polygon against a Z plane with UV interpolation.
// Sutherland-Hodgman: processes each edge. When an intersection is found,
// UVs are linearly interpolated at the same parametric position.
//
// @param verts_in   Input vertices (camera-space)
// @param uvs_in     Input UVs
// @param count_in   Number of input vertices
// @param verts_out  Output vertices (clipped)
// @param uvs_out    Output UVs
// @param count_out  Number of output vertices
// @param z_thresh   The Z threshold for clipping
// @param clip_above If true, keep vertices where z >= z_thresh (near plane).
//                   If false, keep vertices where z <= z_thresh (far plane).
// @return true if at least 3 vertices remain and the polygon is visible.
// ============================================================================
inline bool clip_with_uv(
    const Vec3* verts_in, const Vec2* uvs_in, int count_in,
    Vec3* verts_out, Vec2* uvs_out, int* count_out,
    f32 z_thresh, bool clip_above)
{
    if (count_in < 3) {
        *count_out = 0;
        return false;
    }

    // Local helper: linearly interpolate vertex and UV at the Z-crossing point
    auto intersect = [&](const Vec3& a, const Vec2& uv_a,
                         const Vec3& b, const Vec2& uv_b) -> std::pair<Vec3, Vec2> {
        const f32 dz = b.z - a.z;
        const f32 t = (std::abs(dz) > 1e-12f)
                    ? (z_thresh - a.z) / dz
                    : 0.5f;
        return {
            a + (b - a) * t,
            uv_a + (uv_b - uv_a) * t
        };
    };

    int out_idx = 0;

    for (int i = 0; i < count_in; ++i) {
        const int j = (i + 1) % count_in;
        const Vec3& curr = verts_in[i];
        const Vec3& next = verts_in[j];
        const Vec2& curr_uv = uvs_in[i];
        const Vec2& next_uv = uvs_in[j];

        bool curr_inside, next_inside;
        if (clip_above) {
            curr_inside = curr.z >= z_thresh;
            next_inside = next.z >= z_thresh;
        } else {
            curr_inside = curr.z <= z_thresh;
            next_inside = next.z <= z_thresh;
        }

        if (curr_inside && next_inside) {
            verts_out[out_idx] = next;
            uvs_out[out_idx]   = next_uv;
            out_idx++;
        } else if (curr_inside && !next_inside) {
            // Leaving the plane -> emit intersection only
            auto [mid, mid_uv] = intersect(curr, curr_uv, next, next_uv);
            verts_out[out_idx] = mid;
            uvs_out[out_idx]   = mid_uv;
            out_idx++;
        } else if (!curr_inside && next_inside) {
            // Entering the plane -> emit intersection + next
            auto [mid, mid_uv] = intersect(curr, curr_uv, next, next_uv);
            verts_out[out_idx] = mid;
            uvs_out[out_idx]   = mid_uv;
            out_idx++;

            verts_out[out_idx] = next;
            uvs_out[out_idx]   = next_uv;
            out_idx++;
        }
        // else: both outside -> emit nothing
    }

    *count_out = out_idx;
    return out_idx >= 3;
}

} // namespace chronon3d
