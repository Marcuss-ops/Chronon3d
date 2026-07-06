#pragma once

// ============================================================================
// camera_projection_resolver.hpp — Unified Camera Projection Resolver
//
// @file    camera_projection_resolver.hpp
// @brief   Single projection resolver for all 2.5D layer types.
//
// Every projection path in Chronon3D should eventually go through
// CameraProjectionResolver::project_layer() so that shapes, text, images,
// video, precomps, boxes, and grids all use the same math:
//
//   Layer local space
//   -> world transform (via world_matrix)
//   -> camera view transform (via Camera2_5D::view_matrix())
//   -> near/far plane clipping (Sutherland-Hodgman + UV interpolation)
//   -> 6-plane frustum culling
//   -> perspective projection (focal / depth, Y-down)
//   -> viewport mapping (centred -> screen coords)
//   -> backface detection (camera-space normal)
//
// Coordinate Contract (SINGLE SOURCE OF TRUTH):
// +------------------------------------------------------+
// | World X positive  = right                            |
// | World Y positive  = UP  (GLM Y-up convention)        |
// | World Z positive  = away from camera (LH system)     |
// | Camera forward    = +Z  (look direction)             |
// | Screen origin     = CENTRE (centred coords)          |
// | Framebuffer Y     = DOWN  (top-left origin)          |
// | Rotation order    = ZYX (yaw -> pitch -> roll)       |
// | Focal length     = camera_math::focal_xy_from_camera() |
// | Perspective scale = focal / camera_depth             |
// | Backface test     = camera-space normal.z < 0        |
// | Near plane        = z >= kNearClipZ                  |
// | Far plane         = z <= kFarClipZ                   |
// | Frustum culling   = 6-plane test vs layer bbox       |
// +------------------------------------------------------+
//
// After this resolver is adopted, the four legacy projection paths
// (camera_projection.cpp, camera_2_5d_projection.hpp,
//  projection_context.hpp, projection_utils.cpp) should delegate to it.
//
// FASE 17: secondary static methods (test_frustum_culling,
// compute_signed_area, clip_with_uv, build_perspective_matrix,
// build_projection_matrix) extracted into:
//   - camera_projection_frustum.hpp  (test_frustum_culling + compute_signed_area)
//   - camera_projection_clip.hpp     (clip_with_uv — Sutherland-Hodgman)
//   - camera_projection_matrix.hpp   (backward-compat matrix builders)
//
// Include-ordering note (avoid double-nesting):
//   The three helper headers each open `namespace chronon3d { ... }`
//   themselves.  They are included at FILE SCOPE (between two
//   namespace chronon3d blocks, NOT inside one) to avoid creating
//   `chronon3d::chronon3d::`.  The enum declarations live in the
//   first namespace block so the helpers can reference them.
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

// ============================================================================
// FASE 17 — Secondary helper methods (extracted)
// ============================================================================
//
// Included at file scope (NOT inside any namespace) to avoid double-nesting:
// each helper header opens its own `namespace chronon3d { ... }` so they
// can also be consumed standalone.  Calling code uses unscoped names
// (ADL/name lookup resolves them inside `namespace chronon3d`).

} // namespace chronon3d (close first block — helpers go at file scope)

#include <chronon3d/math/camera_projection_frustum.hpp>
#include <chronon3d/math/camera_projection_clip.hpp>
#include <chronon3d/math/camera_projection_matrix.hpp>

namespace chronon3d {

// ============================================================================
// CameraProjectionInput — Input to CameraProjectionResolver
// ============================================================================
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

// ============================================================================
// ProjectedLayer — Output from CameraProjectionResolver
// ============================================================================
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
// Secondary methods (frustum, clip, matrix builders) live in dedicated
// headers included above (FASE 17).  Only project_layer() remains here.
struct CameraProjectionResolver {

    // -- Primary method: project a single layer --------------------------------
    static ProjectedLayer project_layer(const CameraProjectionInput& input) {
        ProjectedLayer out;

        const camera_math::FocalPx focal_xy = camera_math::focal_xy_from_camera(
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
            out.frustum_result = chronon3d::test_frustum_culling(
                cam_min_x, cam_max_x, cam_min_y, cam_max_y, cam_min_z, cam_max_z,
                focal_xy, input.viewport.width, input.viewport.height,
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
            bool ok = chronon3d::clip_with_uv(
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

            bool ok = chronon3d::clip_with_uv(
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
        // TICKET-035: per-axis perspective scale for anamorphic / Stretch.
        f32 depth_sum = 0.0f;
        f32 max_ps = 0.0f;

        for (int i = 0; i < corner_count; ++i) {
            const Vec3& cp = clipped_corners[i];
            const f32 safe_z = (cp.z > camera_math::kNearClipZ)
                             ? cp.z : camera_math::kNearClipZ;
            const f32 ps_x = (safe_z > 0.0f) ? focal_xy.x / safe_z : 0.0f;
            const f32 ps_y = (safe_z > 0.0f) ? focal_xy.y / safe_z : 0.0f;
            max_ps = std::max(max_ps, ps_y);
            depth_sum += safe_z;

            // Centred screen coords with Y inversion
            out.corners[i] = {
                cp.x * ps_x,    // centred X (per-axis scale)
                -cp.y * ps_y,   // centred Y (INVERTED -- Y-down convention)
                safe_z          // camera-space depth
            };
            out.uvs[i] = clipped_uvs[i];
        }

        out.depth = depth_sum / static_cast<f32>(corner_count);
        out.perspective_scale = max_ps;
        out.visible = corner_count >= 3;

        if (!out.visible) return out;

        // -- 10. Screen-space signed area (for diagnostics, not backface) -------
        out.signed_area = chronon3d::compute_signed_area(out.corners, corner_count);

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
};

} // namespace chronon3d
