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
#include <chronon3d/math/camera_projection_resolver.hpp>
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

/// Delegates to camera_projection_contract.
inline f32 focal_length_from_fov(f32 viewport_height, f32 fov_deg) {
    Camera2_5D tmp;
    tmp.projection_mode = Camera2_5DProjectionMode::Fov;
    tmp.fov_deg = fov_deg;
    return camera_math::focal_from_camera(tmp, viewport_height);
}

/// Delegates to camera_projection_contract.
inline Mat4 get_camera_view_matrix(const Camera2_5D& camera) {
    return camera_math::view_matrix_for_camera(camera);
}

/// Delegates to camera_projection_contract.
/// Now delegates to CameraProjectionResolver for unified math.
inline bool project_world_point_2_5d(
    const Camera2_5D& camera,
    const Mat4& view,
    bool use_view_matrix,
    f32 focal,
    const Vec3& world,
    Vec2& screen,
    f32& depth
) {
    // For single-point projection we use the contract directly
    // (cheaper than building a full resolver input).
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

/// Build the 4×4 perspective projection matrix.
//
//   proj[0][0] = focal       (X scale)
//   proj[1][1] = +focal      (Y scale — no Y inversion; the passive
//                             "out.transform" branch already does
//                             the Y-flip to screen-Y-down)
//   proj[2][3] = 1           (w = z for perspective divide)
//   proj[3][3] = epsilon     (avoids division by zero)
//
// Previously we set proj[1][1] = -focal to match a Y-up↔Y-down convention flip
// at the matrix path, but this broke all existing enable_3d() compositions
// (text disappeared off-screen because both the matrix AND the out.transform
// branches were simultaneously flipping Y).  Now both paths agree again:
// matrix path does NOT flip Y, the out.transform.position.y branch is the
// sole Y-inverter.  This restores pre-session render correctness.
inline Mat4 build_perspective_matrix(f32 focal) {
    Mat4 proj(0.0f);
    proj[0][0] = focal;
    proj[1][1] = +focal;        // no Y inversion in matrix path (matched with passive path)
    proj[2][2] = 1.0f;
    proj[2][3] = 1.0f;          // w = z
    proj[3][3] = 0.0001f;       // epsilon
    return proj;
}

inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Camera2_5D& camera,
    f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled = false
);

/// Project a 3D layer transform through a Camera2_5D into screen space.
///
/// Now delegates to CameraProjectionResolver::project_layer() for unified
/// math.  The output is converted back to ProjectedLayer2_5D for backward
/// compatibility with the existing render graph pipeline.
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
    bool diagnostics_enabled = false
) {
    ProjectedLayer2_5D out;
    out.transform = layer_transform;

    // ── Build input for CameraProjectionResolver ────────────────────────────
    CameraProjectionInput input;
    input.world_transform = layer_matrix;
    input.layer_size = {
        std::abs(layer_transform.scale.x),
        std::abs(layer_transform.scale.y)
    };
    input.camera = camera;
    input.viewport = {viewport_width, viewport_height};
    input.backface_mode = BackfaceMode::DoubleSided;

    // ── Project via the unified resolver ────────────────────────────────────
    auto proj = CameraProjectionResolver::project_layer(input);
    out.visible = proj.visible;

    if (!proj.visible) {
        return out;
    }

    out.depth = proj.depth;
    out.perspective_scale = proj.perspective_scale;

    // ── Recompute centroid for the transform position ──────────────────────
    // Average all projected corners to get the centred screen position.
    Vec2 sum_pos{0.0f, 0.0f};
    Vec2 min_pos{std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()};
    Vec2 max_pos{-std::numeric_limits<f32>::max(), -std::numeric_limits<f32>::max()};

    for (int i = 0; i < proj.corner_count; ++i) {
        sum_pos.x += proj.corners[i].x;
        sum_pos.y += proj.corners[i].y;
        min_pos.x = std::min(min_pos.x, proj.corners[i].x);
        min_pos.y = std::min(min_pos.y, proj.corners[i].y);
        max_pos.x = std::max(max_pos.x, proj.corners[i].x);
        max_pos.y = std::max(max_pos.y, proj.corners[i].y);
    }

    const f32 inv_cnt = 1.0f / static_cast<f32>(proj.corner_count);
    out.transform.position.x = sum_pos.x * inv_cnt;
    out.transform.position.y = sum_pos.y * inv_cnt;
    out.transform.position.z = 0.0f;

    // ── Compute screen-space bbox size for the transform scale ──────────────
    // Use a tiny epsilon floor to avoid degenerate zero-size transforms
    // while still allowing sub-pixel sizes for far layers.
    const f32 bbox_w = std::max(1e-6f, max_pos.x - min_pos.x);
    const f32 bbox_h = std::max(1e-6f, max_pos.y - min_pos.y);
    out.transform.scale.x = bbox_w;
    out.transform.scale.y = bbox_h;

    // ── Build the projection matrix from the resolver ───────────────────────
    const Mat4 view = camera_math::view_matrix_for_camera(camera);
    const f32 focal = camera_math::focal_from_camera(camera, viewport_height);
    const Mat4 proj_mat = CameraProjectionResolver::build_perspective_matrix(focal);
    out.projection_matrix = proj_mat * view * layer_matrix;

    // ── Diagnostic winding check ────────────────────────────────────────────
    if (diagnostics_enabled) {
        glm::mat3 H_diag;
        H_diag[0][0] = out.projection_matrix[0][0]; H_diag[0][1] = out.projection_matrix[0][1]; H_diag[0][2] = out.projection_matrix[0][3];
        H_diag[1][0] = out.projection_matrix[1][0]; H_diag[1][1] = out.projection_matrix[1][1]; H_diag[1][2] = out.projection_matrix[1][3];
        H_diag[2][0] = out.projection_matrix[3][0]; H_diag[2][1] = out.projection_matrix[3][1]; H_diag[2][2] = out.projection_matrix[3][3];
        f32 det = glm::determinant(H_diag);
        detail::log_camera_projection_diagnostics(proj.depth, det);
    }

    return out;
}

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

inline constexpr f32 quad_signed_area(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3) {
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
