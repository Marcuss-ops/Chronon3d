#pragma once

#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/constants.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

struct ProjectedLayer2_5D {
    Transform transform;
    Mat4      projection_matrix{1.0f}; // Matrix that maps world-space to projected screen-space
    f32       depth{1000.0f};
    f32       perspective_scale{1.0f};
    bool      visible{true}; // false when layer is behind or on the camera plane
};

// Returns the focal length (pixels) for a given vertical FOV and viewport height.
// focal = (viewport_height / 2) / tan(fov_rad / 2)
// At depth == focal_length, perspective_scale == 1.
inline f32 focal_length_from_fov(f32 viewport_height, f32 fov_deg) {
    const f32 fov_rad = fov_deg * (math::pi / 180.0f);
    return (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
}

inline Mat4 get_camera_view_matrix(const Camera2_5D& camera) {
    if (camera.point_of_interest_enabled && glm::length(camera.point_of_interest - camera.position) > 0.001f) {
        return math::look_at(camera.position, camera.point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
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

    // look_at flips z (negative = visible); camera_view_matrix and passive both use positive z.
    depth = (use_view_matrix && camera.point_of_interest_enabled) ? -cam_pos.z : cam_pos.z;
    if (depth <= 0.0f) {
        return false;
    }

    screen.x = cam_pos.x * focal / depth;
    screen.y = (use_view_matrix ? -cam_pos.y : cam_pos.y) * focal / depth;
    return true;
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
    out[0][1] = static_cast<f32>(h01);
    out[0][2] = static_cast<f32>(h02);
    out[1][0] = static_cast<f32>(h10);
    out[1][1] = static_cast<f32>(h11);
    out[1][2] = static_cast<f32>(h12);
    out[2][0] = static_cast<f32>(h20);
    out[2][1] = static_cast<f32>(h21);
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
    f32 viewport_height
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
            view = math::translate(Vec3(-camera.position.x, -camera.position.y, -camera.position.z));
        }
        Vec4 world_pos{layer_transform.position.x, layer_transform.position.y, layer_transform.position.z, 1.0f};
        cam_pos = view * world_pos;
    } else {
        // Default mode: passive translation only.
        view = math::translate(Vec3(-camera.position.x, -camera.position.y, -camera.position.z));
        cam_pos.x = layer_transform.position.x - camera.position.x;
        cam_pos.y = layer_transform.position.y - camera.position.y;
        cam_pos.z = layer_transform.position.z - camera.position.z;
    }

    // Only look_at (point_of_interest) produces negative view_z for front-facing points.
    // camera_view_matrix (rotation-only) keeps the passive positive-Z convention.
    const f32 depth = camera.point_of_interest_enabled ? -cam_pos.z : cam_pos.z;

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
    if (use_view_matrix) {
        out.transform.position.x = cam_pos.x * perspective_scale;
        out.transform.position.y = -cam_pos.y * perspective_scale;
    } else {
        out.transform.position.x = layer_transform.position.x - camera.position.x * perspective_scale;
        out.transform.position.y = layer_transform.position.y - camera.position.y * perspective_scale;
    }
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
        // glm::lookAt with Y-up produces right={-1,0,0} in a Y-down screen system,
        // mirroring X. Negate proj[0][0] only for POI path to restore correct handedness.
        // camera_view_matrix (non-POI rotation) already uses the correct convention.
        proj[0][0] = camera.point_of_interest_enabled ? -focal : focal;
        proj[1][1] = focal;
        proj[2][2] = 1.0f;
        // w = +z for camera_view_matrix convention (passive/rotation), -z for look_at (POI)
        proj[2][3] = camera.point_of_interest_enabled ? -1.0f : 1.0f;
        proj[3][3] = 0.0001f;
        out.projection_matrix = proj * view * layer_transform.to_mat4();
    } else {
        out.projection_matrix = out.transform.to_mat4();
    }

    return out;
}

} // namespace chronon3d
