#pragma once

// ============================================================================
// camera_projection_contract.hpp — Unified Camera Projection Contract
//
/// @file    camera_projection_contract.hpp
/// @brief   Single source of truth for all 2.5D camera projection math.
///
/// Every projection path in Chronon3D must go through this contract so that:
///   - Y sign        → screen Y increases downward (origin top-left)
///   - Viewport      → viewport center is ALWAYS added by the contract
///   - Depth         → positive Z = in front of camera (LH system)
///   - FOV / Zoom    → `focal_from_camera()` is the single source of truth
///   - Behind camera → `world_to_camera_space().visible` is the single check
///
/// After this contract is adopted, three layers use it:
///   1. camera_projection.cpp         (Path 1 — `project_world_to_screen`)
///   2. camera_2_5d_projection.hpp    (Path 2 — `project_layer_2_5d`)
///   3. projection_context.hpp         (Path 3 — `ProjectionContext`)
///   4. projection_utils.cpp           (Path 4 — `project_2_5d`)
///
/// All four MUST produce pixel-identical results for the same input.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <cmath>
#include <limits>

namespace chronon3d::camera_math {

// ── Viewport descriptor ─────────────────────────────────────────────────────
// Mirrors the existing Viewport struct but lives in the contract namespace
// to avoid circular dependencies.
struct Viewport2D {
    f32 width{1920.0f};
    f32 height{1080.0f};
};

// ── Camera-space point (after view transform, before projection) ────────────
struct CameraSpacePoint {
    Vec3  position{0.0f};
    f32   depth{0.0f};
    bool  visible{false};
};

// ── Fully projected point (screen-space coordinates) ───────────────────────
struct ProjectedPoint {
    Vec2  centered{0.0f};   // relative to canvas centre
    Vec2  screen{0.0f};     // absolute top-left screen coordinates
    f32   depth{0.0f};
    f32   perspective_scale{1.0f};
    bool  visible{false};
};

// ── Contract: focal length ──────────────────────────────────────────────────
//
// Returns the focal length (in pixels) for a given camera and viewport height.
//
// Rules:
//   - Fov mode: focal = (viewport_height / 2) / tan(fov_deg / 2)
//   - Zoom mode: focal = camera.zoom
//
// At depth == focal the perspective scale is exactly 1.0.
inline f32 focal_from_camera(const Camera2_5D& camera, f32 viewport_height) {
    if (camera.projection_mode == Camera2_5DProjectionMode::Fov) {
        const f32 fov_rad = glm::radians(camera.fov_deg);
        return (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
    }
    return camera.zoom;
}

// ── Contract: view matrix ───────────────────────────────────────────────────
//
// Delegates to Camera2_5D::view_matrix() which handles both POI/lookAt mode
// and rotation-quaternion mode.  This indirection ensures that any future
// changes to the view matrix logic are automatically picked up by all paths.
inline Mat4 view_matrix_for_camera(const Camera2_5D& camera) {
    return camera.view_matrix();
}

// ── Contract: world → camera space ──────────────────────────────────────────
//
// Applies the view transform and returns the camera-space position.
//
// Contract:
//   - visible == true   ⇔  cam.z > near_epsilon
//   - visible == false  ⇔  point is behind or on the camera plane
//   - depth == cam.z    (always positive when visible)
//   - near_epsilon      default 1e-4f (matches existing Path 1 convention)
inline CameraSpacePoint world_to_camera_space(
    const Camera2_5D& camera,
    const Vec3& world,
    f32 near_epsilon = 1e-4f
) {
    const Mat4 view = view_matrix_for_camera(camera);
    const Vec4 cam = view * Vec4(world, 1.0f);

    CameraSpacePoint out;
    out.position = Vec3{cam.x, cam.y, cam.z};
    out.depth = cam.z;
    out.visible = cam.z > near_epsilon;
    return out;
}

// ── Contract: world → screen (full projection) ─────────────────────────────
//
// One-stop function: transforms a world-space point through camera space into
// screen-space pixel coordinates.
//
// Contract:
//   ┌─────────────────────────────────────────────────────────────────┐
//   │ 1. Y sign: INVERTED — screen Y increases downward.              │
//   │    centered.y = -cam.position.y * scale                         │
//   │    screen.y   = centered.y + viewport.height / 2                │
//   │                                                                 │
//   │ 2. Viewport centre: ALWAYS added in screen coordinates.         │
//   │    screen = centered + (viewport.width/2, viewport.height/2)    │
//   │                                                                 │
//   │ 3. Depth: positive Z = in front of camera (LH convention).      │
//   │    visible == false  ⇒  screen is set to -FLT_MAX sentinel.     │
//   │                                                                 │
//   │ 4. perspective_scale = focal / depth                            │
//   └─────────────────────────────────────────────────────────────────┘
inline ProjectedPoint project_world_point(
    const Camera2_5D& camera,
    const Vec3& world,
    Viewport2D viewport,
    f32 near_epsilon = 1e-4f
) {
    ProjectedPoint out;
    const CameraSpacePoint cam = world_to_camera_space(camera, world, near_epsilon);
    out.depth = cam.depth;
    out.visible = cam.visible;

    if (!cam.visible) {
        out.centered = {
            -std::numeric_limits<f32>::max(),
            -std::numeric_limits<f32>::max()
        };
        out.screen = out.centered;
        out.perspective_scale = 0.0f;
        return out;
    }

    const f32 focal = focal_from_camera(camera, viewport.height);
    const f32 scale = focal / cam.depth;
    out.perspective_scale = scale;

    // ── Contract: centered coordinates (no viewport centre, Y inverted) ──
    out.centered = {
        cam.position.x * scale,
        -cam.position.y * scale      // Y sign: inverted (screen Y down)
    };

    // ── Contract: screen coordinates (viewport centre added) ────────────
    out.screen = {
        out.centered.x + viewport.width * 0.5f,
        out.centered.y + viewport.height * 0.5f
    };

    return out;
}

} // namespace chronon3d::camera_math
