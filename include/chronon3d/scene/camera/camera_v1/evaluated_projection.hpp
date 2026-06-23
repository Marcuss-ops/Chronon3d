#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/evaluated_projection.hpp
//
// EvaluatedProjection — normalized snapshot of a ProjectionSpec at a given frame.
//
// Single canonical snapshot produced by CameraProgram from any active
// ProjectionSpec variant (ZoomProjection / FovProjection / PhysicalLensProjection).
// Consumers (framing solver, renderer, projector) read from this snapshot
// instead of recomputing the formulae locally.
//
// CAM-03 / DOC 02 + TICKET-035 goals satisfied by this snapshot:
//   - focal_x_px and focal_y_px are kept separate for anamorphic lenses and
//     `GateFit::Stretch` viewport distortion.
//   - pixel_aspect + anamorphic_squeeze are READ FROM the canonical
//     `LensModel` (no implicit inference from `tan(fov/2)`).
//   - The active viewport reflects the post-gate-fit effective rectangle
//     (smaller than the requested viewport for `GateFit::Overscan`,
//     with pillarbox / letterbox bars).
//   - The output is non-throwing, side-effect-free, pure data.
//
// Hierarchy rule (DOC 02):
//   world_orientation = parent ⊗ base_orientation ⊗ local_offset
// is upheld by CameraProgram::evaluate(); see camera_program.cpp.
// ==============================================================================
#include <chronon3d/math/camera_projection_contract.hpp>  // FocalPx, focal_xy_from_camera, Viewport2D
#include <chronon3d/math/glm_types.hpp>                    // Vec2
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>    // Camera2_5D, CameraOpticsMode, LensModel

#include <cmath>

namespace chronon3d::camera_v1 {

/// Normalized projection snapshot.
/// All values are produced at a given frame from the active ProjectionSpec;
/// the snapshot is pure data and may be safely cached within a render job.
struct EvaluatedProjection {
    /// Effective focal length in pixels along the horizontal axis.
    /// For square-pixels + spherical + Fill/Overscan configurations this
    /// equals focal_y_px.
    float focal_x_px{0.0f};

    /// Effective focal length in pixels along the vertical axis.
    /// This is the canonical reference for perspective_scale = focal_y_px / depth.
    float focal_y_px{0.0f};

    /// Principal point in pixel coordinates (centred convention).
    /// Defaults to viewport_half; the renderer / solver reads this verbatim.
    Vec2 principal_point_px{0.0f, 0.0f};

    /// Effective (post-gate-fit) viewport used when producing this snapshot.
    /// For `GateFit::Overscan` this is SMALLER than the requested viewport
    /// (with letterbox / pillarbox bars excluded).  Callers MUST use these
    /// dimensions; the snapshot is otherwise stale.  TICKET-035.
    camera_math::Viewport2D active_viewport{1920.0f, 1080.0f};

    /// Pixel aspect ratio (1.0 = square pixels).  Sourced from
    /// `LensModel::pixel_aspect` (TICKET-035).  Anamorphic lenses and
    /// non-square pixel grids inflate `focal_x_px` by this value.
    float pixel_aspect{1.0f};

    /// Anamorphic squeeze factor (1.0 = spherical; 2.0 = classic 2×
    /// anamorphic).  Sourced from `LensModel::anamorphic_squeeze`
    /// (TICKET-035).
    float anamorphic_squeeze{1.0f};

    /// True if the active ProjectionSpec variant is PhysicalLensProjection.
    bool is_physical_lens{false};

    /// True if `focal_x_px != focal_y_px` (i.e. `GateFit::Stretch`,
    /// anamorphic squeeze, or non-square pixel aspect is active).
    bool is_anamorphic{false};
};

// ── Factory: produce a snapshot from a fully-evaluated Camera2_5D ───────────
//
// CAM-03 / DOC 02 + TICKET-035 — the renderer / framing-solver / projector
// code paths consume `EvaluatedProjection` snapshots instead of
// recomputing the focal formula locally.  This factory is the single
// canonical producer.
//
// All internal focal derivation goes through the projection contract:
//   `camera_math::focal_xy_from_camera(...)` returns the per-axis (x,y)
//   focal lengths, post-applied with `pixel_aspect * anamorphic_squeeze`
//   on focal_x by `LensModel::focal_xy_pixels`.
//
// The effective viewport is computed via `LensModel::effective_viewport`
// which honours `GateFit::Overscan` (pillarbox / letterbox).
//
// Pre-conditions: `cam` must already have run through
// `CameraProgram::evaluate()` so that `cam.optics_mode` / `cam.lens` /
// `cam.zoom` / `cam.fov_deg` reflect the active ProjectionSpec.
// Inline `tan(fov/2)` is NEVER recomputed in this file (per DOC 02).
inline EvaluatedProjection make_evaluated_projection(const Camera2_5D& cam,
                                                      camera_math::Viewport2D vp) {
    EvaluatedProjection out;

    // ── Effective viewport (TICKET-035) ────────────────────────────────
    // Honours GateFit::Overscan — the active sub-rectangle inside the
    // requested viewport that the sensor image fills.  For Fill / Stretch
    // this equals the requested viewport; for Overscan it's smaller.
    const Vec2 eff = cam.lens.effective_viewport(vp.width, vp.height);
    out.active_viewport = camera_math::Viewport2D{eff.x, eff.y};
    out.principal_point_px = Vec2{eff.x * 0.5f, eff.y * 0.5f};

    // ── Lens-sourced factors (TICKET-035) ──────────────────────────────
    // pixel_aspect and anamorphic_squeeze now come from the canonical
    // LensModel (not silently defaulted to 1.0f as before).
    out.pixel_aspect = cam.lens.pixel_aspect;
    out.anamorphic_squeeze = cam.lens.anamorphic_squeeze;

    // ── Per-axis focal (TICKET-035; single canonical projector) ─────────
    // Route focal through the contract header — no local tan(fov/2), no
    // independent recopy of the anamorphic-squeeze / pixel-aspect math.
    const camera_math::FocalPx focal_xy =
        camera_math::focal_xy_from_camera(cam, vp.width, vp.height);
    out.focal_x_px = focal_xy.x;
    out.focal_y_px = focal_xy.y;

    const bool is_phys = (cam.optics_mode == CameraOpticsMode::PhysicalLens)
                       && (cam.lens.focal_length > 0.0f);
    out.is_physical_lens = is_phys;

    // Anamorphic when focal_x != focal_y (Stretch, anamorphic squeeze,
    // or non-square pixel aspect).
    out.is_anamorphic = std::abs(out.focal_x_px - out.focal_y_px) > 1e-4f;

    return out;
}

} // namespace chronon3d::camera_v1
