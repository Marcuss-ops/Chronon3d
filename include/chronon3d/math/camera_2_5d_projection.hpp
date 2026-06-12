#pragma once

// ============================================================================
// camera_2_5d_projection.hpp — 2.5D Layer Projection (Render Path)
//
/// @file    camera_2_5d_projection.hpp
/// @brief   Project a 2.5D layer transform through a Camera2_5D into screen
///          space for the render graph pipeline.
///
/// All core math now delegates to camera_projection_contract.hpp so that
/// every projection path in Chronon3D uses the same sign, centre, depth,
/// and FOV/zoom conventions.
///
/// Backward-compatible wrappers are provided for `focal_length_from_fov()`,
/// `get_camera_view_matrix()`, and `project_world_point_2_5d()`.
// ============================================================================

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/math/near_plane_clip.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <array>

namespace chronon3d::detail {
void log_camera_projection_diagnostics(float depth, float determinant);
}

namespace chronon3d {

struct ProjectedLayer2_5D {
    Transform transform;           // screen-space transform (centered coords)
    Mat4      projection_matrix{1.0f}; // proj * view * model (maps world → centered screen)
    f32       depth{1000.0f};      // camera-space Z (positive = visible)
    f32       perspective_scale{1.0f};
    bool      visible{true};       // false when layer is behind or on the camera plane
};

// ── Backward-compatible wrapper ─────────────────────────────────────────────
// Delegates to the contract.
inline f32 focal_length_from_fov(f32 viewport_height, f32 fov_deg) {
    Camera2_5D tmp;
    tmp.projection_mode = Camera2_5DProjectionMode::Fov;
    tmp.fov_deg = fov_deg;
    return camera_math::focal_from_camera(tmp, viewport_height);
}

// ── Backward-compatible wrapper ─────────────────────────────────────────────
// Delegates to the contract.
inline Mat4 get_camera_view_matrix(const Camera2_5D& camera) {
    return camera_math::view_matrix_for_camera(camera);
}

// ── Backward-compatible wrapper ─────────────────────────────────────────────
// Delegates to the contract.
inline bool project_world_point_2_5d(
    const Camera2_5D& camera,
    const Mat4& view,
    bool use_view_matrix,
    f32 focal,
    const Vec3& world,
    Vec2& screen,
    f32& depth
) {
    // If a caller passes an explicit view and focal, respect those
    // (legacy tests and some diagnostic code call this directly).
    // Otherwise fall back to the contract.
    if (use_view_matrix) {
        Vec4 cam_pos = view * Vec4(world, 1.0f);
        depth = cam_pos.z;
        if (depth <= 0.0f) return false;
        // Contract Y sign: inverted
        screen.x = cam_pos.x * focal / depth;
        screen.y = -cam_pos.y * focal / depth;
        return true;
    }

    // Translation-only path (no view matrix)
    Vec4 cam_pos{0.0f, 0.0f, 0.0f, 1.0f};
    cam_pos.x = world.x - camera.position.x;
    cam_pos.y = world.y - camera.position.y;
    cam_pos.z = world.z - camera.position.z;
    depth = cam_pos.z;
    if (depth <= 0.0f) return false;

    screen.x = cam_pos.x * focal / depth;
    screen.y = -cam_pos.y * focal / depth;  // Contract Y sign: inverted
    return true;
}

// ── Internal helper: build the 4×4 perspective projection matrix ───────────
//
// The matrix maps camera-space coords to centred-screen coords.
//   proj[0][0] = focal       (X scale)
//   proj[1][1] = -focal      (Y scale — inverted, matching contract Y sign)
//   proj[2][3] = 1           (w = z for perspective divide)
//   proj[3][3] = epsilon     (avoids division by zero)
//
// Result: after perspective divide, centred_screen.x = focal * cam.x / cam.z
//         centred_screen.y = -focal * cam.y / cam.z  ← inverted Y
inline Mat4 build_perspective_matrix(f32 focal) {
    Mat4 proj(0.0f);
    proj[0][0] = focal;
    proj[1][1] = -focal;        // ← Contract: inverted Y
    proj[2][2] = 1.0f;
    proj[2][3] = 1.0f;          // w = z
    proj[3][3] = 0.0001f;       // epsilon
    return proj;
}

// ── Convenience overload (derives layer_matrix from transform) ─────────────
inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Camera2_5D& camera,
    f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled = false
);

// ── Main projection function ───────────────────────────────────────────────
//
/// Project a 3D layer transform through a Camera2_5D into screen space.
///
/// Returns a ProjectedLayer2_5D with:
///   - transform.position     → centred screen coords  (origin at canvas centre)
///   - transform.scale        → scaled by perspective_scale
///   - projection_matrix      → full 4×4 perspective proj * view * model
///   - depth                  → camera-space Z
///   - perspective_scale      → focal / depth
///   - visible                → false when behind camera
///
/// The projection_matrix is designed to be passed to TransformNode::set_matrix()
/// which then wraps it with canvas offsets (dst_canvas_offset / src_canvas_offset)
/// for final pixel-accurate rendering.
inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Mat4& layer_matrix,
    const Camera2_5D& camera,
    f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled
) {
    ProjectedLayer2_5D out;
    out.transform = layer_transform;

    // ── Camera-space position ──────────────────────────────────────────
    Mat4 view{1.0f};
    Vec4 cam_pos{0.0f, 0.0f, 0.0f, 1.0f};
    Vec4 world_pos = layer_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);

    const bool has_rotation = glm::length(camera.rotation) > 0.0001f;
    const bool layer_has_rotation = std::abs(layer_transform.rotation.w - 1.0f) > 0.0001f;
    const bool use_view_matrix = camera.point_of_interest_enabled || has_rotation || layer_has_rotation;

    if (use_view_matrix) {
        // Full view matrix path (handles rotation, POI, and layer rotation)
        if (has_rotation || camera.point_of_interest_enabled) {
            view = camera_math::view_matrix_for_camera(camera);
        } else {
            view = glm::translate(Mat4(1.0f), Vec3(-camera.position.x, -camera.position.y, -camera.position.z));
        }
        cam_pos = view * world_pos;
    } else {
        // Default: passive translation only — no view matrix overhead.
        cam_pos.x = world_pos.x - camera.position.x;
        cam_pos.y = world_pos.y - camera.position.y;
        cam_pos.z = world_pos.z - camera.position.z;
    }

    const f32 depth = cam_pos.z;

    // ── Focal length (shared by all paths) ─────────────────────────────
    const f32 focal = camera_math::focal_from_camera(camera, viewport_height);

    // ── Near-plane check: test the 4 camera-space corners ──────────────
    //
    // The layer is a flat quad in the XY plane at local Z = 0.
    // We transform the 4 corners to camera space and check each against
    // the near plane (z = kNearClipZ from near_plane_clip.hpp).
    //
    // Cases:
    //   ALL corners behind near plane → invisible (fast cull)
    //   ALL corners in front of near plane → fast path (current, ~95%)
    //   SOME corners behind, some in front → clip & fallback (rare, unstable)
    const f32 half_w = std::abs(layer_transform.scale.x) * 0.5f;
    const f32 half_h = std::abs(layer_transform.scale.y) * 0.5f;
    // Use a fixed 100x100 unit quad if scale is degenerate
    const f32 qhw = (half_w > 1e-4f) ? half_w : 50.0f;
    const f32 qhh = (half_h > 1e-4f) ? half_h : 50.0f;

    const Vec3 local_corners[4] = {
        {-qhw, -qhh, 0.0f}, { qhw, -qhh, 0.0f},
        { qhw,  qhh, 0.0f}, {-qhw,  qhh, 0.0f},
    };

    // Transform corners to camera space
    const bool need_cam_corners = use_view_matrix || (depth <= camera_math::kNearClipZ);
    std::array<Vec3, 4> cam_corners{};
    bool any_behind = false;
    bool all_behind = true;

    if (need_cam_corners) {
        for (int i = 0; i < 4; ++i) {
            Vec4 world_c = layer_matrix * Vec4(local_corners[i], 1.0f);
            Vec4 cam_c = use_view_matrix ? (view * world_c) : world_c;
            if (!use_view_matrix) {
                cam_c.x -= camera.position.x;
                cam_c.y -= camera.position.y;
                cam_c.z -= camera.position.z;
            }
            cam_corners[i] = {cam_c.x, cam_c.y, cam_c.z};
            const bool behind = cam_corners[i].z <= camera_math::kNearClipZ;
            any_behind = any_behind || behind;
            all_behind = all_behind && behind;
        }
    } else {
        // Fast path: centroid is in front, no view matrix → no corners needed
        all_behind = false;
        any_behind = (depth <= camera_math::kNearClipZ);
    }

    // ── All corners behind → invisible ─────────────────────────────────
    if (all_behind) {
        out.visible = false;
        return out;
    }

    // ── Some corners behind → clip against near plane (rare) ────────────
    if (any_behind) {
        auto clipped = camera_math::clip_quad_against_near_plane(cam_corners);
        if (!clipped.visible || clipped.count < 3) {
            out.visible = false;
            return out;
        }

        // Project each clipped camera-space point to centred screen coords
        f32 min_x = std::numeric_limits<f32>::max();
        f32 max_x = -std::numeric_limits<f32>::max();
        f32 min_y = std::numeric_limits<f32>::max();
        f32 max_y = -std::numeric_limits<f32>::max();

        for (int i = 0; i < clipped.count; ++i) {
            const Vec3& cp = clipped.points[i];
            const f32 pz = (cp.z > 0.0f) ? cp.z : camera_math::kNearClipZ;
            const f32 cps = focal / pz;
            const f32 sx = cp.x * cps;
            const f32 sy = -cp.y * cps;  // Contract: inverted Y
            min_x = std::min(min_x, sx);
            max_x = std::max(max_x, sx);
            min_y = std::min(min_y, sy);
            max_y = std::max(max_y, sy);
        }

        // Stable fallback: conservative bbox centered at (min+max)/2
        const f32 bbox_cx = (min_x + max_x) * 0.5f;
        const f32 bbox_cy = (min_y + max_y) * 0.5f;
        const f32 bbox_w = std::max(1.0f, max_x - min_x);
        const f32 bbox_h = std::max(1.0f, max_y - min_y);

        out.transform.position.x = bbox_cx;
        out.transform.position.y = bbox_cy;
        out.transform.position.z = 0.0f;
        out.transform.scale.x = bbox_w;
        out.transform.scale.y = bbox_h;
        out.depth = std::max(1.0f, depth);
        out.perspective_scale = focal / std::max(1.0f, depth);
        out.visible = true;

        // Build a simple affine projection matrix for the clipped quad
        // Maps from source framebuffer coords → centred screen bbox
        Mat4 clip_proj(1.0f);
        clip_proj[0][0] = bbox_w;                   // X scale
        clip_proj[1][1] = -bbox_h;                   // Y scale (inverted)
        clip_proj[2][2] = 1.0f;
        clip_proj[3][0] = bbox_cx;                   // X translation (centred)
        clip_proj[3][1] = bbox_cy;                   // Y translation (centred)
        out.projection_matrix = clip_proj;

        return out;
    }

    // ── ALL corners in front → fast path (no clipping needed) ───────────
    // ── Focal & perspective scale ──────────────────────────────────────
    const f32 ps = focal / depth;

    // ── Contract: centred screen position with inverted Y ──────────────
    out.transform.position.x =  cam_pos.x * ps;   // centred X
    out.transform.position.y = -cam_pos.y * ps;   // centred Y — inverted
    out.transform.position.z = 0.0f;

    out.transform.scale.x *= ps;
    out.transform.scale.y *= ps;

    out.depth             = depth;
    out.perspective_scale = ps;

    // ── Build projection matrix ────────────────────────────────────────
    if (use_view_matrix) {
        // Full perspective: proj * view * model → maps world to centred screen
        const Mat4 proj = build_perspective_matrix(focal);
        out.projection_matrix = proj * view * layer_matrix;

        // Diagnostic winding / homography determinant check
        {
            glm::mat3 H_diag;
            H_diag[0][0] = out.projection_matrix[0][0]; H_diag[0][1] = out.projection_matrix[0][1]; H_diag[0][2] = out.projection_matrix[0][3];
            H_diag[1][0] = out.projection_matrix[1][0]; H_diag[1][1] = out.projection_matrix[1][1]; H_diag[1][2] = out.projection_matrix[1][3];
            H_diag[2][0] = out.projection_matrix[3][0]; H_diag[2][1] = out.projection_matrix[3][1]; H_diag[2][2] = out.projection_matrix[3][3];
            f32 det = glm::determinant(H_diag);
            if (diagnostics_enabled) {
                detail::log_camera_projection_diagnostics(depth, det);
            }
        }
    } else {
        // Passive path: use the simple TRS transform (no perspective skew needed)
        out.projection_matrix = out.transform.to_mat4();
    }

    return out;
}

// ── Convenience overload ───────────────────────────────────────────────────
inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Camera2_5D& camera,
    f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled
) {
    return project_layer_2_5d(layer_transform, layer_transform.to_mat4(),
                              camera, viewport_width, viewport_height,
                              diagnostics_enabled);
}

// ── Utility: signed area of a projected quad ───────────────────────────────
inline f32 quad_signed_area(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3) {
    const f32 twice_area =
        p0.x * p1.y - p1.x * p0.y +
        p1.x * p2.y - p2.x * p1.y +
        p2.x * p3.y - p3.x * p2.y +
        p3.x * p0.y - p0.x * p3.y;
    return twice_area * 0.5f;
}

inline std::optional<f32> projected_quad_signed_area(
    const Mat4& matrix,
    f32 width,
    f32 height
) {
    const Vec4 corners[4] = {
        matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f),
        matrix * Vec4(width, 0.0f, 0.0f, 1.0f),
        matrix * Vec4(width, height, 0.0f, 1.0f),
        matrix * Vec4(0.0f, height, 0.0f, 1.0f)
    };

    Vec2 projected[4];
    for (int i = 0; i < 4; ++i) {
        if (std::abs(corners[i].w) < 1e-6f) {
            return std::nullopt;
        }
        projected[i] = {corners[i].x / corners[i].w, corners[i].y / corners[i].w};
    }

    return quad_signed_area(projected[0], projected[1], projected[2], projected[3]);
}

// ── Homography solvers ─────────────────────────────────────────────────────
inline bool solve_homography_4pt(const Vec2 src[4], const Vec2 dst[4], glm::mat3& out) {
    double a[8][9]{};
    for (int i = 0; i < 4; ++i) {
        const double x = src[i].x;
        const double y = src[i].y;
        const double u = dst[i].x;
        const double v = dst[i].y;

        const int r = i * 2;
        a[r][0] = x;   a[r][1] = y;   a[r][2] = 1.0;
        a[r][6] = -u * x; a[r][7] = -u * y; a[r][8] = u;

        a[r + 1][3] = x; a[r + 1][4] = y; a[r + 1][5] = 1.0;
        a[r + 1][6] = -v * x; a[r + 1][7] = -v * y; a[r + 1][8] = v;
    }

    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        double best = std::abs(a[pivot][col]);
        for (int row = col + 1; row < 8; ++row) {
            const double cand = std::abs(a[row][col]);
            if (cand > best) {
                best = cand;
                pivot = row;
            }
        }
        if (best < 1e-9) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 9; ++k) {
                std::swap(a[pivot][k], a[col][k]);
            }
        }

        const double inv_pivot = 1.0 / a[col][col];
        for (int k = col; k < 9; ++k) {
            a[col][k] *= inv_pivot;
        }

        for (int row = 0; row < 8; ++row) {
            if (row == col) continue;
            const double factor = a[row][col];
            if (std::abs(factor) < 1e-12) continue;
            for (int k = col; k < 9; ++k) {
                a[row][k] -= factor * a[col][k];
            }
        }
    }

    const double h00 = a[0][8];
    const double h01 = a[1][8];
    const double h02 = a[2][8];
    const double h10 = a[3][8];
    const double h11 = a[4][8];
    const double h12 = a[5][8];
    const double h20 = a[6][8];
    const double h21 = a[7][8];

    out = glm::mat3(1.0f);
    out[0][0] = static_cast<f32>(h00);
    out[0][1] = static_cast<f32>(h10);
    out[0][2] = static_cast<f32>(h20);
    out[1][0] = static_cast<f32>(h01);
    out[1][1] = static_cast<f32>(h11);
    out[1][2] = static_cast<f32>(h21);
    out[2][0] = static_cast<f32>(h02);
    out[2][1] = static_cast<f32>(h12);
    out[2][2] = 1.0f;
    return true;
}

inline Mat4 pack_homography_3x3_to_4x4(const glm::mat3& h) {
    Mat4 out(1.0f);
    out[0][0] = h[0][0];
    out[0][1] = h[0][1];
    out[0][3] = h[0][2];
    out[1][0] = h[1][0];
    out[1][1] = h[1][1];
    out[1][3] = h[1][2];
    out[2][2] = 1.0f;
    out[3][0] = h[2][0];
    out[3][1] = h[2][1];
    out[3][3] = h[2][2];
    return out;
}

} // namespace chronon3d
