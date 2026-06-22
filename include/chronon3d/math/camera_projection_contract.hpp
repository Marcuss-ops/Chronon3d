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
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
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

// ── Canonical focal formula (CAM-03 / DOC 02) ───────────────────────────────
//
// DO NOT RECOMPUTE `(viewport_h/2) / tan(fov/2)` ANYWHERE ELSE.
//
// This is the SINGLE legal implementation of the per-FOV focal formula
// in the codebase.  All callers (rendering, framing, projection, composition
// validators) MUST delegate to `camera_math::focal_from_camera()` instead of
// recomputing the tangent locally.  The helper exists solely so that the
// trigonometric step lives in exactly one place and one place only.
//
// !!! INTERNAL — use `camera_math::focal_from_camera()` instead !!!
// New production code MUST NOT call `pixel_focal_from_fov` directly; the
// only consumer is the `FieldOfView` branch of `focal_from_camera()`.
// The function is `inline` only because it lives in a header, NOT because
// it is meant for callers outside the contract.  If you find yourself
// reaching for this helper, route through `focal_from_camera()` so the
// optics_mode switch (PhysicalLens / Fov / Zoom / legacy DoF fallback)
// stays the single source of truth.
inline f32 pixel_focal_from_fov(f32 fov_rad, f32 viewport_height) {
    return (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
}

// ── Contract: focal length (pixels) ──────────────────────────────────────────
//
// Returns the focal length in pixels for a given camera and viewport dimensions.
//
// Rules:
//   - PhysicalLens mode (canonical):  derived from Camera2_5D::lens with
//     gate-fit.  Independent of DoF — a 24mm or 135mm lens changes the
//     perspective even when camera.dof.enabled is false (AE contract).
//   - FieldOfView mode (canonical):   focal = (viewport_height/2) / tan(fov/2)
//     — centralizzato in `pixel_focal_from_fov()`.
//   - Zoom mode (canonical):          focal = camera.get_zoom().
//   - Legacy DoF fallback:            if optics_mode is the default Zoom but
//     camera.get_dof_use_physical_model() is true, the legacy path is still honoured.
//
// At depth == focal the perspective scale is exactly 1.0.
inline f32 focal_from_camera(const CameraProjectionSource& camera, f32 viewport_width, f32 viewport_height) {
    // Canon: switch purely on optics_mode — optics != DoF.
    switch (camera.get_optics_mode()) {
        case CameraOpticsMode::PhysicalLens: {
            if (camera.get_lens().focal_length > 0.0f) {
                return camera.get_lens().focal_pixels(viewport_width, viewport_height);
            }
            return camera.get_zoom();  // degenerate lens → fall back to zoom
        }
        case CameraOpticsMode::FieldOfView: {
            const f32 fov_rad = glm::radians(camera.get_fov_deg());
            return pixel_focal_from_fov(fov_rad, viewport_height);
        }
        case CameraOpticsMode::Zoom:
            break;
    }
    if (camera.get_zoom() > 0.0f) {
        return camera.get_zoom();
    }
    // Legacy DoF-driven fallback for callers that pre-date CameraOpticsMode.
    if (camera.get_lens().focal_length > 0.0f && camera.get_dof_use_physical_model()) {
        return camera.get_lens().focal_pixels(viewport_width, viewport_height);
    }
    if (camera.get_projection_mode() == Camera2_5DProjectionMode::Fov) {
        const f32 fov_rad = glm::radians(camera.get_fov_deg());
        return pixel_focal_from_fov(fov_rad, viewport_height);
    }
    return camera.get_zoom();
}

// Single-arg overload for backward compat with callers that don't yet pass viewport_width.
// Legacy code paths use only viewport_height (Fov/Zoom modes ignore viewport_width).
// For lens mode, this is a fallback — prefer the 2-arg overload with real viewport dims.
inline f32 focal_from_camera(const CameraProjectionSource& camera, f32 viewport_height) {
    return focal_from_camera(camera, viewport_height, viewport_height);
}

// ── Contract: view matrix ───────────────────────────────────────────────────
//
// Delegates to Camera2_5D::view_matrix() which handles both POI/lookAt mode
// and rotation-quaternion mode.  This indirection ensures that any future
// changes to the view matrix logic are automatically picked up by all paths.
inline Mat4 view_matrix_for_camera(const CameraProjectionSource& camera) {
    return camera.get_view_matrix();
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
    const CameraProjectionSource& camera,
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
    const CameraProjectionSource& camera,
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

    const f32 focal = focal_from_camera(camera, viewport.width, viewport.height);
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
