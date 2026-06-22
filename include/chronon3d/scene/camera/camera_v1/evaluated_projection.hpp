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
// CAM-03 / DOC 02 goals satisfied by this snapshot:
//   - focal_x_px and focal_y_px are kept separate for anamorphic lenses.
//   - Anamorphic squeeze and pixel aspect are recorded explicitly
//     (no implicit inference from `tan(fov/2)`).
//   - The viewport is recorded so parity checks pivot on the same dimensions.
//   - The output is non-throwing, side-effect-free, pure data.
//
// Hierarchy rule (DOC 02):
//   world_orientation = parent ⊗ base_orientation ⊗ local_offset
// is upheld by CameraProgram::evaluate(); see camera_program.cpp.
// ==============================================================================
#include <chronon3d/math/camera_projection_contract.hpp>  // Viewport2D, focal_from_camera
#include <chronon3d/math/glm_types.hpp>                    // Vec2
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>    // Camera2_5D, CameraOpticsMode

#include <cmath>

namespace chronon3d::camera_v1 {

/// Normalized projection snapshot.
/// All values are produced at a given frame from the active ProjectionSpec;
/// the snapshot is pure data and may be safely cached within a render job.
struct EvaluatedProjection {
    /// Effective focal length in pixels along the horizontal axis.
    /// For square-pixels configurations this equals focal_y_px.
    float focal_x_px{0.0f};

    /// Effective focal length in pixels along the vertical axis.
    /// This is the canonical reference for perspective_scale = focal_y_px / depth.
    float focal_y_px{0.0f};

    /// Principal point in pixel coordinates (centred convention).
    /// Defaults to viewport_half; the renderer / solver reads this verbatim.
    Vec2 principal_point_px{0.0f, 0.0f};

    /// Viewport used when producing this snapshot.
    /// Callers MUST use exactly these dimensions; the snapshot is otherwise stale.
    camera_math::Viewport2D active_viewport{1920.0f, 1080.0f};

    /// Pixel aspect ratio (1.0 = square pixels).  Anamorphic lenses inflate
    /// focal_x_px by this value to keep aspect-ratio-correct horizontal FOV.
    float pixel_aspect{1.0f};

    /// Anamorphic squeeze factor as supplied by PhysicalLensProjection.
    /// 1.0 = spherical; 2.0 = classic 2× anamorphic stretch.
    float anamorphic_squeeze{1.0f};

    /// True if the active ProjectionSpec variant is PhysicalLensProjection.
    bool is_physical_lens{false};

    /// True if focal_x_px != focal_y_px (i.e. anamorphic squeeze active).
    bool is_anamorphic{false};
};

// ── Factory: produce a snapshot from a fully-evaluated Camera2_5D ────────────
//
// CAM-03 / DOC 02 — the renderer/framing-solver/projector code paths
// consume `EvaluatedProjection` snapshots instead of recomputing the focal
// formula locally.  This factory is the single canonical producer.
//
// Pre-conditions: `cam` must already have run through
// `CameraProgram::evaluate()` so that `cam.optics_mode` / `cam.lens` /
// `cam.zoom` / `cam.fov_deg` reflect the active ProjectionSpec.
// All internal focal derivation goes through `camera_math::focal_from_camera()`.
// Inline `tan(fov/2)` is NEVER recomputed in this file (per DOC 02).
inline EvaluatedProjection make_evaluated_projection(const Camera2_5D& cam,
                                                      camera_math::Viewport2D vp) {
    EvaluatedProjection out;
    out.active_viewport = vp;
    out.principal_point_px = Vec2{vp.width * 0.5f, vp.height * 0.5f};

    // Route focal through the canonical contract — no local tan(fov/2).
    // CAM-03 / DOC 02 — direct field access on Camera2_5D (no getter):
    //   optics_mode is publicly readable; lens is a public member, not
    //   behind an accessor (verified in scene/model/camera/camera_2_5d.hpp).
    const bool is_phys = (cam.optics_mode == CameraOpticsMode::PhysicalLens)
                       && (cam.lens.focal_length > 0.0f);
    out.is_physical_lens = is_phys;
    out.focal_y_px = camera_math::focal_from_camera(cam, vp.width, vp.height);

    // Anamorphic stretch applied to focal_x only (horizontal axis).
    // `pixel_aspect` and `anamorphic_squeeze` default to 1.0f; a future
    // PhysicalLensProjection with custom squeeze will populate these.
    out.focal_x_px = out.focal_y_px * out.pixel_aspect * out.anamorphic_squeeze;
    out.is_anamorphic = std::abs(out.focal_x_px - out.focal_y_px) > 1e-4f;
    return out;
}

} // namespace chronon3d::camera_v1
