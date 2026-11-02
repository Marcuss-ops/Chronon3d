#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace chronon3d {

/// Single fail-closed optics admission policy: a non-zero `iris_blades` has no
/// kernel (neither the separable nor the disk DOF kernel builds a blade
/// polygon aperture). `highlight_gain` IS implemented on every path
/// (`highlight_weight()` in dof_blur.comp, `dof_highlight_weight()` in the
/// software kernel), so it is accepted here and consumed by the kernels.
/// This is the one seat of the policy: the render_plan camera contract and the
/// per-node render-boundary gate below are callers of this predicate, not a
/// second copy of the rule.
[[nodiscard]] inline bool dof_shape_unsupported(int iris_blades, f32 highlight_gain) {
    (void)highlight_gain;
    return iris_blades != 0;
}

/// Fail closed when the authored lens shaping has no kernel support. No DOF
/// kernel builds an iris-blade polygon aperture, so `iris_blades` would be
/// silently ignored and render as plain circular bokeh. Call this at the render
/// boundary of every path that applies a DOF blur so unsupported shaping fails
/// loudly instead of silently mismatching the authored lens.
inline void reject_unsupported_dof_shape(
    const DepthOfFieldSettings& dof) {
    constexpr std::string_view kAdviceView =
        " (the DOF kernels support circular bokeh with highlight weighting "
        "only); remove the iris_blades value from the plan or implement the "
        "shaped kernels first";
    if (dof_shape_unsupported(static_cast<int>(dof.iris_blades), dof.highlight_gain)) {
        throw std::runtime_error(
            "DOF: unsupported shaped bokeh (iris_blades " +
            std::to_string(dof.iris_blades) + ") is authored but no DOF kernel "
            "implements iris-shaped bokeh" + std::string{kAdviceView});
    }
}

/// Sentinel stored in the per-pixel DOF depth plane when no visible surface
/// contributed to a pixel. Zero is not a valid positive camera-space optical
/// depth and must never be reused to mean "unset".
inline constexpr float kUnsetDofDepth = 1e18f;


/// Compute the physical Circle of Confusion radius in scene units (mm)
/// for an object at positive camera-space depth given lens parameters.
///
/// Physical model (thin-lens approximation):
///   CoC = A * |S2 - S1| / S2 * m
///   where:
///     A  = lens.focal_length / lens.f_stop  (aperture diameter in scene units)
///     S1 = dof.focus_distance              (distance to focus plane)
///     S2 = camera-space depth             (distance to object)
///     m  = lens.focal_length / (S1 - lens.focal_length)  (magnification at focus plane)
///
/// The result is converted from scene units to pixel radius by the caller
/// via the sensor width ratio.
[[nodiscard]] inline f32 compute_physical_coc(
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    f32 camera_depth
) {
    const f32 s1 = dof.focus_distance;
    const f32 s2 = camera_depth;

    if (!std::isfinite(s1) || !std::isfinite(s2) || s2 <= 1e-4f ||
        s1 < lens.focal_length + 1e-4f || lens.f_stop <= 0.0f ||
        lens.sensor_width <= 0.0f) {
        return 0.0f;
    }

    // Aperture diameter and magnification (from LensModel).
    const f32 A = lens.focal_length / lens.f_stop;
    const f32 m = lens.focal_length / (s1 - lens.focal_length);

    // CoC diameter on the sensor (in scene units, same as lens units).
    const f32 coc_mm = A * std::abs(s2 - s1) / s2 * m;

    // Convert to pixel radius: CoC fraction of sensor * viewport width.
    // We don't know the viewport here, so return scene-unit CoC.
    // Callers scale by (viewport_width / sensor_width).
    return coc_mm;
}

// Camera-space depth at an authored world point.
[[nodiscard]] inline f32 camera_space_depth(
    const Camera2_5DRuntime& camera,
    const Vec3& world_position) {
    return (camera.view_matrix() * Vec4{world_position, 1.0f}).z;
}

/// Compute the thin-lens Circle of Confusion radius in pixels for a positive
/// camera-space depth.
[[nodiscard]] inline f32 compute_dof_blur_radius(
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    f32 camera_depth,
    f32 viewport_width = 1920.0f
) {
    if (!dof.enabled || !std::isfinite(camera_depth) || camera_depth <= 0.0f ||
        !std::isfinite(viewport_width) || viewport_width <= 0.0f) {
        return 0.0f;
    }

    const f32 coc_mm = compute_physical_coc(dof, lens, camera_depth);
    if (coc_mm <= 0.0f || lens.sensor_width <= 0.0f) return 0.0f;
    const f32 pixels = coc_mm * (viewport_width / lens.sensor_width);
    const bool is_near = camera_depth < dof.focus_distance;
    const f32 bokeh_limit = (is_near && dof.near_bokeh_radius > 0.0f) ? dof.near_bokeh_radius
                          : (!is_near && dof.far_bokeh_radius > 0.0f)  ? dof.far_bokeh_radius
                          : dof.max_blur;
    return std::clamp(pixels, 0.0f, std::max(0.0f, bokeh_limit));
}

} // namespace chronon3d
