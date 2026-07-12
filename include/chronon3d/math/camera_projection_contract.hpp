#pragma once

// ============================================================================
// camera_projection_contract.hpp — Unified Camera Projection Contract
//
// @file    camera_projection_contract.hpp
// @brief   Single source of truth for all 2.5D camera projection math.
//
// Every projection path in Chronon3D MUST go through this contract so that:
//   - Y sign        → screen Y increases downward (origin top-left)
//   - Viewport      → viewport center is ALWAYS added by the contract
//   - Depth         → positive Z = in front of camera (LH system)
//   - FOV / Zoom    → `focal_xy_from_camera()` is the single source of truth
//                     (returns per-axis focal x and y; the legacy scalar
//                     `focal_from_camera(...)` is a thin wrapper)
//   - Behind camera → `out.visible == false` is the single check
//
// After this contract is adopted, four layers use it:
//   1. camera_projection.cpp         (Path 1 — `project_world_to_screen`)
//   2. camera_2_5d_projection.hpp    (Path 2 — `project_layer_2_5d`)
//   3. projection_context.hpp         (Path 3 — `ProjectionContext`)
//   4. projection_utils.cpp           (Path 4 — `project_2_5d`)
//
// All four MUST produce bit-identical results for the same input.
//
// TICKET-035 — focal x/y + GateFit::Stretch + Overscan active viewport +
// pixel_aspect / anamorphic_squeeze from `LensModel` + visibility/coordinate
// separation for invisible points.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <cmath>
#include <limits>

namespace chronon3d::camera_math {

// ── Viewport descriptor ────────────────────────────────────────────────────
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
//
/// TICKET-035 — Validity/coordinate separation:
    //   * If `visible == false`, the `centered` and `screen` fields hold safe
    //     ZERO defaults (NOT numeric sentinels).  Callers MUST treat them as
    //     meaning-less "valid == false" territory and never project them as
    //     scene geometry.  The single source of truth is `visible`.
//   * If `visible == true`, `centered` is relative to canvas centre, `screen`
//     has the canvas-centre offset applied, and `perspective_scale` is the
//     Y-axis reference (focal_y_px / depth).
struct ProjectedPoint {
    Vec2  centered{0.0f};          // relative to canvas centre (Y inverted: screen-Y down)
    Vec2  screen{0.0f};            // absolute top-left screen coordinates
    f32   depth{0.0f};
    f32   perspective_scale{1.0f}; // canonical: focal_y_px / depth
    bool  visible{false};          // SOURCE OF TRUTH for invisibility; never trust coords when false.
};

// ── Focal-length-per-axis result (TICKET-035) ───────────────────────────────
//
// Returned by `focal_xy_from_camera(...)`.  `x` is the horizontal focal in
// pixels (colloquially `focal_x_px`); `y` is the vertical focal in pixels
// (`focal_y_px`, the canonical Y reference for `perspective_scale`).
//
// For Fill / Overscan with a spherical lens and square pixels, `x == y`.
// For `GateFit::Stretch`, anamorphic lenses, or non-square pixel grids,
// `x != y` — callers MUST apply the per-axis scale (`scale_x = x / depth`,
// `scale_y = y / depth`) so aspect ratio is preserved on the canvas.
struct FocalPx {
    f32 x{0.0f};   // horizontal focal length in pixels
    f32 y{0.0f};   // vertical focal length in pixels (canonical reference)
};

// ── Projection contract config (TICKET-PROJECTION-V1 forward-point 0e+) ─────
//
// Centralises the mutable tunables of the unified projection contract so a
// caller can override the visibility/near-plane epsilon (or any future
// contract constant) at call time WITHOUT having to thread an extra `f32`
// parameter.  The struct is plain-data (mirrors the `LensModel` precedent —
// default-value member initialisation + aggregate-initialisation-friendly)
// and the existing `near_epsilon`-keyed overloads stay as thin forwarders
// so ABI / call-site compat is preserved.
//
//   ProjectionContractConfig cfg{};                          // canonical defaults
//   ProjectionContractConfig cfg{ProjectionContractConfig{1e-3f}};  // custom near
//   world_to_camera_space(camera, world, cfg);               // config overload
//
// All defaults match the previous hardcoded values (1e-4f near_epsilon), so
// switching an existing call site from the f32-keyed overload to the config
// overload with a default-constructed config is semantically a no-op.
struct ProjectionContractConfig {
    /// Below this depth a camera-space point is considered "behind" or
    /// "on the camera plane" and `visible == false`.  Default `1e-4f`
    /// matches the pre-existing Path 1 convention.  Override only if the
    /// caller needs a coarser near-plane (e.g. depth-precision-limited
    /// software backends or test scenarios probing the boundary).
    f32 near_epsilon{1e-4f};
};

/// Canonical default factory — trivial wrapper, kept for symmetry with the
/// `LensPresets` namespace so authoring code can use a uniform style.
[[nodiscard]] inline ProjectionContractConfig default_projection_contract_config() noexcept {
    return {};
}

// ── Contract: focal length (x and y, per axis) ─────────────────────────────
//
// TICKET-035 — produces per-axis focal lengths without creating a second
// projector.  All four projection paths (`project_world_point`,
// `project_layer_2_5d`, `make_projection_context`, `project_2_5d`) MUST
// route through this function before any `scale = focal/depth` math.
//
// Rules (mirrors the legacy `focal_from_camera` projection_mode /
// optics_mode priority):
//   - PhysicalLens mode (canonical):   focal_xy derived from cam.lens
//                                       via `LensModel::focal_xy_pixels`.
//                                       Honors `pixel_aspect` +
//                                       `anamorphic_squeeze` on focal_x.
//   - FieldOfView mode (canonical):    focal_x = focal_y = (vp.h/2)/tan(fov/2).
//   - Fov projection_mode (priority):  same as FieldOfView.
//   - Zoom mode (canonical):           focal_x = focal_y = cam.zoom.
//   - Legacy DoF fallback:             focal via `LensModel::focal_xy_pixels`.
//
// Per-axis scaling (`scale_x`, `scale_y`) for `GateFit::Stretch` and
// anamorphic lenses happens in `LensModel::focal_xy_pixels()` + the lens
// factors (`pixel_aspect` and `anamorphic_squeeze`).  The contract returns
// the post-factored values so callers cannot accidentally drop the X-axis.
inline FocalPx focal_xy_from_camera(const CameraProjectionSource& camera,
                                    f32 viewport_width, f32 viewport_height) {
    FocalPx out{1000.0f, 1000.0f};

    // Canon: switch purely on optics_mode.
    switch (camera.get_optics_mode()) {
        case CameraOpticsMode::PhysicalLens: {
            if (camera.get_lens().focal_length > 0.0f) {
                const Vec2 fxy = camera.get_lens().focal_xy_pixels(
                    viewport_width, viewport_height);
                out.x = fxy.x;
                out.y = fxy.y;
                return out;
            }
            // Degenerate lens → fall back to zoom (consistent across X and Y).
            out.x = camera.get_zoom();
            out.y = camera.get_zoom();
            return out;
        }
        case CameraOpticsMode::FieldOfView: {
            const f32 fov_rad = glm::radians(camera.get_fov_deg());
            const f32 f = (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
            out.x = f;
            out.y = f;
            return out;
        }
        case CameraOpticsMode::Zoom:
            break;
    }
    if (camera.get_zoom() > 0.0f) {
        out.x = camera.get_zoom();
        out.y = camera.get_zoom();
        return out;
    }
    // Legacy DoF-driven fallback.
    if (camera.get_lens().focal_length > 0.0f && camera.get_dof_use_physical_model()) {
        const Vec2 fxy = camera.get_lens().focal_xy_pixels(
            viewport_width, viewport_height);
        out.x = fxy.x;
        out.y = fxy.y;
        return out;
    }
    // No viewport-aware derivation succeeded → keep default {1000,1000}.
    return out;
}

// Single-arg overload for backward compat with callers that only know
// viewport_height (legacy code paths).  Fov/Zoom modes ignore viewport_width;
// for lens mode it is a fallback — prefer the 2-arg overload.
inline f32 focal_from_camera(const CameraProjectionSource& camera,
                             f32 viewport_width, f32 viewport_height) {
    return focal_xy_from_camera(camera, viewport_width, viewport_height).y;
}

// Single-arg overload for callers that don't pass viewport_width separately.
inline f32 focal_from_camera(const CameraProjectionSource& camera,
                             f32 viewport_height) {
    return focal_xy_from_camera(camera, viewport_height, viewport_height).y;
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

// TICKET-PROJECTION-V1 forward-point 0e+ — config-based overload.  This is
// the canonical going-forward signature: callers that want to override
// `near_epsilon` (or any future contract constant) without an extra f32
// parameter pass a `ProjectionContractConfig`.  Semantically a no-op when
// the config is default-constructed (forwards to the f32 overload with
// `near_epsilon = cfg.near_epsilon`).
inline CameraSpacePoint world_to_camera_space(
    const CameraProjectionSource& camera,
    const Vec3& world,
    ProjectionContractConfig cfg
) {
    return world_to_camera_space(camera, world, cfg.near_epsilon);
}

// ── Contract: world → screen (full projection) ─────────────────────────────
//
// One-stop function: transforms a world-space point through camera space into
// screen-space pixel coordinates.
//
// Contract (TICKET-035):
//   ┌─────────────────────────────────────────────────────────────────┐
//   │ 1. Y sign: INVERTED — screen Y increases downward.              │
//   │    centered.y = -cam.position.y * scale_y                        │
//   │    screen.y   = centered.y + viewport.height / 2                │
//   │                                                                 │
//   │ 2. Viewport centre: ALWAYS added in screen coordinates.         │
//   │    screen = centered + (viewport.width/2, viewport.height/2)    │
//   │                                                                 │
//   │ 3. Validity/coord separation (TICKET-035):                      │
//   │    visible == false  ⇒  centered = screen = {0,0},              │
//   │    perspective_scale = 0.0f, depth = cam.depth.                  │
//   │    Callers MUST NOT use centered/screen as geometry when         │
//   │    visible == false.  The single source of truth is .visible.    │
//   │    No numeric sentinels like -FLT_MAX are emitted.               │
//   │                                                                 │
//   │ 4. Per-axis perspective scale (TICKET-035):                      │
//   │    scale_x = focal_xy.x / depth       (X axis)                   │
//   │    scale_y = focal_xy.y / depth       (Y axis, canonical; used   │
//   │                                            as perspective_scale)│
//   │    For Fill/Overscan + spherical + square pixels, scale_x == scale_y│
//   │    For Stretch / anamorphic / non-square pixels, scale_x != scale_y│
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
        // ── TICKET-035: validity/coord separation ──
        // Coords set to safe ZERO defaults; the single source of truth
        // for invisibility is `out.visible`.  Callers MUST check
        // `visible` BEFORE consuming centered/screen as geometry.
        out.centered = {0.0f, 0.0f};
        out.screen = {0.0f, 0.0f};
        out.perspective_scale = 0.0f;
        return out;
    }

    const FocalPx focal_xy = focal_xy_from_camera(camera, viewport.width, viewport.height);
    const f32 scale_x = focal_xy.x / cam.depth;
    const f32 scale_y = focal_xy.y / cam.depth;
    // Canonical perspective_scale uses focal_y_px (TICKET-035; preserves the
    // focal/depth invariant tested by the existing parity tests).
    out.perspective_scale = scale_y;

    // Center coordinates: per-axis scale (X and Y independent).
    out.centered = {
        cam.position.x * scale_x,
        -cam.position.y * scale_y      // Y sign: inverted (screen Y down)
    };

    // Screen coordinates: viewport centre added.
    out.screen = {
        out.centered.x + viewport.width * 0.5f,
        out.centered.y + viewport.height * 0.5f
    };

    return out;
}

// TICKET-PROJECTION-V1 forward-point 0e+ — config-based overload of the
// full projection.  Symmetric with the `world_to_camera_space` config
// overload above; default-constructed config is semantically a no-op.
inline ProjectedPoint project_world_point(
    const CameraProjectionSource& camera,
    const Vec3& world,
    Viewport2D viewport,
    ProjectionContractConfig cfg
) {
    return project_world_point(camera, world, viewport, cfg.near_epsilon);
}

} // namespace chronon3d::camera_math
