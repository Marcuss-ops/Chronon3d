#pragma once

// ============================================================================
// camera_projection_resolver.hpp — Unified Camera Projection Resolver
//
/// @file    camera_projection_resolver.hpp
/// @brief   Single projection resolver for all 2.5D layer types.
///
/// Every projection path in Chronon3D should eventually go through
/// CameraProjectionResolver::project_layer() so that shapes, text, images,
/// video, precomps, boxes, and grids all use the same math:
///
///   Layer local space
///   -> world transform (via world_matrix)
///   -> camera view transform (via Camera2_5D::view_matrix())
///   -> near/far plane clipping (Sutherland-Hodgman + UV interpolation)
///   -> 6-plane frustum culling
///   -> perspective projection (focal / depth, Y-down)
///   -> viewport mapping (centred -> screen coords)
///   -> backface detection (camera-space normal)
///
/// Coordinate Contract (SINGLE SOURCE OF TRUTH):
/// +------------------------------------------------------+
/// | World X positive  = right                            |
/// | World Y positive  = UP  (GLM Y-up convention)        |
/// | World Z positive  = away from camera (LH system)     |
/// | Camera forward    = +Z  (look direction)             |
/// | Screen origin     = CENTRE (centred coords)          |
/// | Framebuffer Y     = DOWN  (top-left origin)          |
/// | Rotation order    = ZYX (yaw -> pitch -> roll)       |
/// | Focal formula     = (viewport_h/2) / tan(fov/2)      |
/// | Perspective scale = focal / camera_depth             |
/// | Backface test     = camera-space normal.z < 0        |
/// | Near plane        = z >= kNearClipZ                  |
/// | Far plane         = z <= kFarClipZ                   |
/// | Frustum culling   = 6-plane test vs layer bbox       |
/// +------------------------------------------------------+
///
/// After this resolver is adopted, the four legacy projection paths
/// (camera_projection.cpp, camera_2_5d_projection.hpp,
///  projection_context.hpp, projection_utils.cpp) should delegate to it.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/math/near_plane_clip.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>

namespace chronon3d {

// -- Far plane constant --------------------------------------------------------
inline constexpr f32 kFarClipZ = 1e6f;  // Default far plane (very large)

// -- Backface policy -----------------------------------------------------------
enum class BackfaceMode : uint8_t {
    Visible       = 0,   // Always render (double-sided), NO correction
    Hidden        = 1,   // Cull when back-facing (camera-space normal.z < 0)
    DoubleSided   = 2,   // Render both sides -- mirror UVs on back-face
};

// -- Clip state ----------------------------------------------------------------
enum class ClipState : uint8_t {
    NotClipped    = 0,   // All vertices inside both near and far planes
    ClippedNear   = 1,   // Clipped against near plane
    ClippedFar    = 2,   // Clipped against far plane
    ClippedBoth   = 3,   // Clipped against both near and far planes
    AllBehind     = 4,   // All vertices behind near plane -> invisible
    AllBeyond     = 5,   // All vertices beyond far plane -> invisible
};

// -- Frustum culling result ----------------------------------------------------
enum class FrustumResult : uint8_t {
    Inside        = 0,   // Layer is fully inside the view frustum
    Intersects    = 1,   // Layer intersects frustum boundary (partially visible)
    Outside       = 2,   // Layer is fully outside the view frustum -> cull
};

// -- Input to CameraProjectionResolver -----------------------------------------
struct CameraProjectionInput {
    Mat4      world_transform{1.0f};   // Layer's world-space 4x4 matrix
    Vec2      layer_size{100.0f, 100.0f}; // Unscaled width x height of layer
    Camera2_5D camera;                  // Active camera
    camera_math::Viewport2D viewport{}; // Output viewport dimensions

    BackfaceMode backface_mode{BackfaceMode::DoubleSided};

    // Far-plane distance in camera-space.  Defaults to a very large value.
    // Set to a smaller value to enable far-plane clipping.
    f32 far_plane{kFarClipZ};

    // If true, performs 6-plane frustum culling and sets frustum_result.
    // When the layer is fully outside the frustum, visible=false.
    bool enable_frustum_culling{false};
};

// -- Output from CameraProjectionResolver ---------------------------------------
struct ProjectedLayer {
    // Screen-space corners (max 6 after quadrilateral -> hexagon clipping).
    // Each corner has .x and .y as centred screen coordinates (origin at
    // canvas centre), and .z as camera-space depth (positive = visible).
    // Order is TL -> TR -> BR -> BL (or clipped polygon winding).
    Vec3  corners[6]{};
    Vec2  uvs[6]{};              // UV coordinates matching each corner
    int   corner_count{0};

    f32   depth{0.0f};           // Average camera-space depth (for z-sorting)
    f32   perspective_scale{1.0f};

    bool  visible{false};        // true when at least 3 corners are in front
    bool  back_facing{false};    // true when camera-space normal.z < 0

    ClipState     clip_state{ClipState::NotClipped};
    FrustumResult frustum_result{FrustumResult::Inside};
    f32           signed_area{0.0f}; // Projected 2D signed area (screen-space)
};

// ============================================================================
// CameraProjectionResolver
// ============================================================================
//
// Thread-safe: no mutable state -- all outputs are returned by value.
struct CameraProjectionResolver {

    // -- Primary method: project a single layer --------------------------------
    static ProjectedLayer project_layer(const CameraProjectionInput& input) {
        ProjectedLayer out;

        const f32 focal = camera_math::focal_from_camera(
            input.camera, input.viewport.width, input.viewport.height);
        const Mat4 view = camera_math::view_matrix_for_camera(input.camera);

        // -- 1. Compute 4 local corners (TL, TR, BR, BL) ------------------------
        const f32 hw = input.layer_size.x * 0.5f;
        const f32 hh = input.layer_size.y * 0.5f;
        const Vec3 local_corners[4] = {
            {-hw, -hh, 0.0f},  // TL
            { hw, -hh, 0.0f},  // TR
            { hw,  hh, 0.0f},  // BR
            {-hw,  hh, 0.0f},  // BL
        };
        const Vec2 local_uvs[4] = {
            {0.0f, 0.0f},  // TL
            {1.0f, 0.0f},  // TR
            {1.0f, 1.0f},  // BR
            {0.0f, 1.0f},  // BL
        };

        // -- 2. Transform to camera space ---------------------------------------
        std::array<Vec3, 4> cam_corners{};
        std::array<Vec2, 4> cam_uvs{};
        for (int i = 0; i < 4; ++i) {
            Vec4 world = input.world_transform * Vec4(local_corners[i], 1.0f);
            Vec4 cam   = view * world;
            cam_corners[i] = {cam.x, cam.y, cam.z};
            cam_uvs[i]     = local_uvs[i];
        }

        // -- 3. Camera-space backface detection ---------------------------------
        {
            const Vec3 e1 = cam_corners[1] - cam_corners[0];
            const Vec3 e2 = cam_corners[3] - cam_corners[0];
            const Vec3 normal = glm::cross(e1, e2);
            out.back_facing = normal.z < 0.0f;
        }

        // -- 4. Compute camera-space bounding box for frustum culling -----------
        f32 cam_min_x = cam_corners[0].x, cam_max_x = cam_corners[0].x;
        f32 cam_min_y = cam_corners[0].y, cam_max_y = cam_corners[0].y;
        f32 cam_min_z = cam_corners[0].z, cam_max_z = cam_corners[0].z;
        for (int i = 1; i < 4; ++i) {
            cam_min_x = std::min(cam_min_x, cam_corners[i].x);
            cam_max_x = std::max(cam_max_x, cam_corners[i].x);
            cam_min_y = std::min(cam_min_y, cam_corners[i].y);
            cam_max_y = std::max(cam_max_y, cam_corners[i].y);
            cam_min_z = std::min(cam_min_z, cam_corners[i].z);
            cam_max_z = std::max(cam_max_z, cam_corners[i].z);
        }

        // -- 5. 6-plane frustum culling (optional) -----------------------------
        if (input.enable_frustum_culling) {
            out.frustum_result = test_frustum_culling(
                cam_min_x, cam_max_x, cam_min_y, cam_max_y, cam_min_z, cam_max_z,
                focal, input.viewport.width, input.viewport.height,
                input.far_plane);
            if (out.frustum_result == FrustumResult::Outside) {
                out.visible = false;
                return out;
            }
        }

        // -- 6. Near-plane check + optional clipping ---------------------------
        out.clip_state = ClipState::NotClipped;
        bool any_near = false, all_near = true;
        for (int i = 0; i < 4; ++i) {
            const bool behind = cam_corners[i].z <= camera_math::kNearClipZ;
            any_near = any_near || behind;
            all_near = all_near && behind;
        }

        if (all_near) {
            out.clip_state = ClipState::AllBehind;
            out.visible = false;
            return out;
        }

        // -- 7. Far-plane check -------------------------------------------------
        bool any_far = false, all_far = true;
        if (input.far_plane > camera_math::kNearClipZ) {
            for (int i = 0; i < 4; ++i) {
                const bool beyond = cam_corners[i].z >= input.far_plane;
                any_far = any_far || beyond;
                all_far = all_far && beyond;
            }
        } else {
            all_far = false;
        }

        if (all_far) {
            out.clip_state = ClipState::AllBeyond;
            out.visible = false;
            return out;
        }

        // -- 8. Clip against near and far planes --------------------------------
        int corner_count = 4;
        Vec3 clipped_corners[6]{};
        Vec2 clipped_uvs[6]{};

        // First pass: clip against near plane (require z >= nearZ)
        if (any_near) {
            bool ok = clip_with_uv(
                cam_corners.data(), cam_uvs.data(), 4,
                clipped_corners, clipped_uvs, &corner_count,
                camera_math::kNearClipZ, true);
            if (!ok || corner_count < 3) {
                out.visible = false;
                return out;
            }
            out.clip_state = ClipState::ClippedNear;
        } else {
            for (int i = 0; i < 4; ++i) {
                clipped_corners[i] = cam_corners[i];
                clipped_uvs[i]     = cam_uvs[i];
            }
            corner_count = 4;
        }

        // Second pass: clip against far plane (require z <= farZ)
        // This runs on the near-clipped result
        if (any_far && input.far_plane > camera_math::kNearClipZ) {
            Vec3 far_clipped[6]{};
            Vec2 far_uvs[6]{};
            int far_count = 0;

            bool ok = clip_with_uv(
                clipped_corners, clipped_uvs, corner_count,
                far_clipped, far_uvs, &far_count,
                input.far_plane, false);

            if (!ok || far_count < 3) {
                out.clip_state = ClipState::AllBeyond;
                out.visible = false;
                return out;
            }

            // Update clip state
            if (out.clip_state == ClipState::ClippedNear) {
                out.clip_state = ClipState::ClippedBoth;
            } else {
                out.clip_state = ClipState::ClippedFar;
            }

            corner_count = far_count;
            for (int i = 0; i < far_count; ++i) {
                clipped_corners[i] = far_clipped[i];
                clipped_uvs[i]     = far_uvs[i];
            }
        }

        out.corner_count = corner_count;

        // -- 9. Perspective projection to centred screen coords ----------------
        f32 depth_sum = 0.0f;
        f32 max_ps = 0.0f;

        for (int i = 0; i < corner_count; ++i) {
            const Vec3& cp = clipped_corners[i];
            const f32 safe_z = (cp.z > camera_math::kNearClipZ)
                             ? cp.z : camera_math::kNearClipZ;
            const f32 ps = (safe_z > 0.0f) ? focal / safe_z : 0.0f;
            max_ps = std::max(max_ps, ps);
            depth_sum += safe_z;

            // Centred screen coords with Y inversion
            out.corners[i] = {
                cp.x * ps,      // centred X
                -cp.y * ps,     // centred Y (INVERTED -- Y-down convention)
                safe_z          // camera-space depth
            };
            out.uvs[i] = clipped_uvs[i];
        }

        out.depth = depth_sum / static_cast<f32>(corner_count);
        out.perspective_scale = max_ps;
        out.visible = corner_count >= 3;

        if (!out.visible) return out;

        // -- 10. Screen-space signed area (for diagnostics, not backface) -------
        out.signed_area = compute_signed_area(out.corners, corner_count);

        // -- 11. Apply backface policy ------------------------------------------
        if (out.back_facing) {
            switch (input.backface_mode) {
                case BackfaceMode::Hidden:
                    out.visible = false;
                    return out;

                case BackfaceMode::DoubleSided:
                    for (int i = 0; i < corner_count; ++i) {
                        out.uvs[i].y = 1.0f - out.uvs[i].y;
                    }
                    break;

                case BackfaceMode::Visible:
                default:
                    break;
            }
        }

        return out;
    }

    // -- 6-plane frustum culling test -------------------------------------------
    // Tests the camera-space bounding box against the 6 frustum planes.
    // The frustum is defined by the FOV and viewport aspect ratio.
    // Returns Inside / Intersects / Outside.
    static FrustumResult test_frustum_culling(
        f32 min_x, f32 max_x,
        f32 min_y, f32 max_y,
        f32 min_z, f32 max_z,
        f32 focal, f32 vp_width, f32 vp_height,
        f32 far_plane = kFarClipZ)
    {
        const f32 tan_half_fov_v = (vp_height * 0.5f) / focal;
        const f32 tan_half_fov_h = (vp_width  * 0.5f) / focal;

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
                return FrustumResult::Outside;  // all 8 corners outside this plane
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

    // -- Utility: compute 2D signed area of a polygon --------------------------
    static f32 compute_signed_area(const Vec3* vertices, int count) {
        if (count < 3) return 0.0f;
        f32 area = 0.0f;
        for (int i = 0; i < count; ++i) {
            const int j = (i + 1) % count;
            area += vertices[i].x * vertices[j].y;
            area -= vertices[j].x * vertices[i].y;
        }
        return area * 0.5f;
    }

    // -- Clip a convex polygon against a Z plane with UV interpolation ---------
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
    static bool clip_with_uv(
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

    // -- Build the 4x4 perspective projection matrix (backward compat) ---------
    static Mat4 build_perspective_matrix(f32 focal) {
        Mat4 proj(0.0f);
        proj[0][0] = focal;
        proj[1][1] = -focal;
        proj[2][2] = 1.0f;
        proj[2][3] = 1.0f;
        proj[3][3] = 0.0001f;
        return proj;
    }

    // -- Build complete proj * view * model matrix (backward compat) -----------
    static Mat4 build_projection_matrix(
        const Camera2_5D& camera, const Mat4& world_transform,
        f32 viewport_width, f32 viewport_height)
    {
        const f32 focal = camera_math::focal_from_camera(
            camera, viewport_width, viewport_height);
        const Mat4 view = camera_math::view_matrix_for_camera(camera);
        return build_perspective_matrix(focal) * view * world_transform;
    }
};

} // namespace chronon3d
