#pragma once

// ============================================================================
// camera_projection_matrix.hpp — Backward-compat matrix builders
//
// @file    camera_projection_matrix.hpp
// @brief   build_perspective_matrix() + build_projection_matrix() helpers.
//          Convenience for legacy code paths that want a single combined
//          proj * view * model matrix instead of the structured
//          project_layer() output.
//
// Extracted from camera_projection_resolver.hpp (FASE 17) to keep the
// primary resolver focused on project_layer() itself.  All functions
// are inline and live in the same namespace chronon3d as the parent.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>

namespace chronon3d {

// ============================================================================
// CameraProjectionResolver::build_perspective_matrix
//
// Build the 4x4 perspective projection matrix (backward compat).
// TICKET-035: per-axis focal_x / focal_y for anamorphic / Stretch.
// ============================================================================
inline Mat4 build_perspective_matrix(const camera_math::FocalPx& focal_xy) {
    Mat4 proj(0.0f);
    proj[0][0] = focal_xy.x;
    proj[1][1] = -focal_xy.y;
    proj[2][2] = 1.0f;
    proj[2][3] = 1.0f;
    proj[3][3] = 0.0001f;
    return proj;
}

// ============================================================================
// CameraProjectionResolver::build_projection_matrix
//
// Build complete proj * view * model matrix (backward compat).
// Equivalent to (Mat4 product) but as a single convenience call.
// ============================================================================
inline Mat4 build_projection_matrix(
    const CameraProjectionSource& camera, const Mat4& world_transform,
    f32 viewport_width, f32 viewport_height)
{
    const camera_math::FocalPx focal_xy = camera_math::focal_xy_from_camera(
        camera, viewport_width, viewport_height);
    const Mat4 view = camera_math::view_matrix_for_camera(camera);
    return build_perspective_matrix(focal_xy) * view * world_transform;
}

} // namespace chronon3d
