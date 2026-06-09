#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <glm/gtc/constants.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <spdlog/spdlog.h>

namespace chronon3d {

struct ProjectedLayer2_5D {
    Transform transform;
    Mat4      projection_matrix{1.0f}; // Matrix that maps world-space to projected screen-space
    f32       depth{1000.0f};
    f32       perspective_scale{1.0f};
    bool      visible{true}; // false when layer is behind or on the camera plane
};

inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Mat4& layer_matrix,
    const Camera2_5D& camera,
    [[maybe_unused]] f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled = false
);

// Returns the focal length (pixels) for a given vertical FOV and viewport height.
// focal = (viewport_height / 2) / tan(fov_rad / 2)
// At depth == focal_length, perspective_scale == 1.
inline f32 focal_length_from_fov(f32 viewport_height, f32 fov_deg) {
    const f32 fov_rad = fov_deg * (glm::pi<float>() / 180.0f);
    return (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
}

inline Mat4 get_camera_view_matrix(const Camera2_5D& camera) {
    if (camera.point_of_interest_enabled && glm::length(camera.point_of_interest - camera.position) > 0.001f) {
        return glm::lookAtLH(camera.position, camera.point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
    }
    return math::camera_view_matrix(camera.position, camera.rotation_quaternion());
}

inline bool project_world_point_2_5d(
    const Camera2_5D& camera,
    const Mat4& view,
    bool use_view_matrix,
    f32 focal,
    const Vec3& world,
    Vec2& screen,
    f32& depth
) {
    Vec4 cam_pos{0.0f, 0.0f, 0.0f, 1.0f};
    if (use_view_matrix) {
        cam_pos = view * Vec4(world, 1.0f);
    } else {
        cam_pos.x = world.x - camera.position.x;
        cam_pos.y = world.y - camera.position.y;
        cam_pos.z = world.z - camera.position.z;
    }

    // Now that glm::lookAt is used, all conventions produce positive Z for visible points.
    depth = cam_pos.z;
    if (depth <= 0.0f) {
        return false;
    }

    screen.x = cam_pos.x * focal / depth;
    screen.y = cam_pos.y * focal / depth;
    return true;
}

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

// Project a 3D layer transform through a Camera2_5D into screen space.
inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Camera2_5D& camera,
    [[maybe_unused]] f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled = false
) {
    return project_layer_2_5d(layer_transform, layer_transform.to_mat4(), camera, viewport_width, viewport_height, diagnostics_enabled);
}

inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Mat4& layer_matrix,
    const Camera2_5D& camera,
    [[maybe_unused]] f32 viewport_width,
    f32 viewport_height,
    bool diagnostics_enabled
) {
    ProjectedLayer2_5D out;
    out.transform = layer_transform;

    // Position of the layer in camera space.
    Mat4 view{1.0f};
    Vec4 cam_pos{0.0f, 0.0f, 0.0f, 1.0f};
    const bool has_rotation = glm::length(camera.rotation) > 0.0001f;
    const bool layer_has_rotation = std::abs(layer_transform.rotation.w - 1.0f) > 0.0001f;
    const bool use_view_matrix = camera.point_of_interest_enabled || has_rotation || layer_has_rotation;

    if (use_view_matrix) {
        // Use the full view matrix when the camera is rotating, orbiting, or the layer has rotation.
        if (has_rotation || camera.point_of_interest_enabled) {
            view = get_camera_view_matrix(camera);
        } else {
            // Matrix path forced by layer rotation, but camera is straight.
            view = glm::translate(Mat4(1.0f), Vec3(-camera.position.x, -camera.position.y, -camera.position.z));
        }
        Vec4 world_pos = layer_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        cam_pos = view * world_pos;
    } else {
        // Default mode: passive translation only.
        view = glm::translate(Mat4(1.0f), Vec3(-camera.position.x, -camera.position.y, -camera.position.z));
        Vec4 world_pos = layer_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        cam_pos.x = world_pos.x - camera.position.x;
        cam_pos.y = world_pos.y - camera.position.y;
        cam_pos.z = world_pos.z - camera.position.z;
    }

    const f32 depth = cam_pos.z;

    // Cull layers that are behind or touching the camera plane.
    if (depth <= 0.0f) {
        out.visible = false;
        return out;
    }

    const f32 focal = (camera.projection_mode == Camera2_5DProjectionMode::Fov)
        ? focal_length_from_fov(viewport_height, camera.fov_deg)
        : camera.zoom;

    const f32 perspective_scale = focal / depth;

    // Centroid projection: translate to screen-space (origin = top-left).
    // viewport center is added so shapes using model[3] as screen coords work.
    out.transform.position.x = cam_pos.x * perspective_scale;
    out.transform.position.y = cam_pos.y * perspective_scale;
    out.transform.position.z = 0.0f;

    out.transform.scale.x *= perspective_scale;
    out.transform.scale.y *= perspective_scale;

    out.depth             = depth;
    out.perspective_scale = perspective_scale;

    // For view-matrix (rotation/POI) path: build a full perspective projection matrix.
    // proj * view * model maps world vertices to centered screen coords (w = depth).
    // For passive path: use the simple TRS transform (no perspective skew needed).
    if (use_view_matrix) {
        Mat4 proj = Mat4(0.0f);
        // Both X and Y axes project with positive signs, matching screen-space LHS.
        proj[0][0] = focal;
        proj[1][1] = focal;
        proj[2][2] = 1.0f;
        // w = +z for all conventions now.
        proj[2][3] = 1.0f;
        proj[3][3] = 0.0001f;
        out.projection_matrix = proj * view * layer_matrix;

        // Diagnostic Winding and Homography Determinant Check
        glm::mat3 H_diag;
        H_diag[0][0] = out.projection_matrix[0][0]; H_diag[0][1] = out.projection_matrix[0][1]; H_diag[0][2] = out.projection_matrix[0][3];
        H_diag[1][0] = out.projection_matrix[1][0]; H_diag[1][1] = out.projection_matrix[1][1]; H_diag[1][2] = out.projection_matrix[1][3];
        H_diag[2][0] = out.projection_matrix[3][0]; H_diag[2][1] = out.projection_matrix[3][1]; H_diag[2][2] = out.projection_matrix[3][3];
        f32 det = glm::determinant(H_diag);
        if (diagnostics_enabled) {
            spdlog::info("[diagnostics-3d] Compiled projection matrix. Depth={:.2f}, Det={:.4f} ({})",
                         depth, det, (det >= 0.0f ? "OK" : "FLIPPED/MIRRORED"));
        }
    } else {
        out.projection_matrix = out.transform.to_mat4();
    }

    return out;
}

} // namespace chronon3d
