#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

/// Compute the physical Circle of Confusion radius in scene units (mm)
/// for a point at `layer_world_z` given the lens parameters.
///
/// **Important**: this function assumes the camera is at world origin (z=0).
/// Callers with a non-origin camera (the default Chronon3d camera is at
/// z=-1000) should offset `layer_world_z` by `camera.position.z` before
/// calling, so that `layer_world_z` represents the true signed distance
/// from the camera along the optical axis.
///
/// Physical model (thin-lens approximation):
///   CoC = A * |S2 - S1| / S2 * m
///   where:
///     A  = focal_length / f_stop          (aperture diameter in scene units)
///     S1 = focus_distance                 (distance to focus plane)
///     S2 = |layer_world_z|                (distance to object)
///     m  = focal_length / (S1 - focal_length)  (magnification at focus plane)
///
/// The result is converted from scene units to pixel radius by the caller
/// via the sensor width ratio.
[[nodiscard]] inline f32 compute_physical_coc(const DepthOfFieldSettings& dof, f32 layer_world_z) {
    const f32 s1 = std::abs(dof.focus_distance);
    const f32 s2 = std::abs(layer_world_z);

    if (s2 < 1e-4f || s1 < dof.focal_length + 1e-4f) {
        return 0.0f;
    }

    // Aperture diameter and magnification.
    const f32 A = dof.focal_length / dof.f_stop;
    const f32 m = dof.focal_length / (s1 - dof.focal_length);

    // CoC diameter on the sensor (in scene units, same as lens units).
    const f32 coc_mm = A * std::abs(s2 - s1) / s2 * m;

    // Convert to pixel radius: CoC fraction of sensor * viewport width.
    // We don't know the viewport here, so return scene-unit CoC.
    // Callers scale by (viewport_width / sensor_width).
    return coc_mm;
}

/// Compute the DoF blur radius in pixels for a layer at `layer_world_z`.
///
/// When `use_physical_model` is true, uses the thin-lens Circle of Confusion
/// model.  Otherwise falls back to the legacy linear blur formula.
///
/// The optional `viewport_width` parameter is used to scale the physical CoC
/// from sensor-mm to pixels.  Defaults to 1920 (common HD width).
[[nodiscard]] inline f32 compute_dof_blur_radius(
    const DepthOfFieldSettings& dof,
    f32 layer_world_z,
    f32 viewport_width = 1920.0f
) {
    if (!dof.enabled) {
        return 0.0f;
    }

    if (dof.use_physical_model) {
        const f32 coc_mm = compute_physical_coc(dof, layer_world_z);
        if (coc_mm <= 0.0f) return 0.0f;
        // Scale CoC from sensor-mm to pixels.
        f32 pixels = coc_mm * (viewport_width / dof.sensor_width);
        // Per-side bokeh clamp: if near/far limits are set, clamp based on
        // whether the layer is nearer or farther than the focus plane.
        // NOTE: this heuristic assumes the camera is near z=0 (the coordinate
        // system origin). For cameras at other positions, abs(layer_world_z)
        // should be replaced with the true distance from the camera.
        const bool is_near = std::abs(layer_world_z) < std::abs(dof.focus_distance);
        const f32 bokeh_limit = (is_near && dof.near_bokeh_radius > 0.0f) ? dof.near_bokeh_radius
                              : (!is_near && dof.far_bokeh_radius > 0.0f)  ? dof.far_bokeh_radius
                              : dof.max_blur;
        const f32 clamp_max = bokeh_limit > 0.0f ? bokeh_limit : 100.0f;
        return std::clamp(pixels, 0.0f, clamp_max);
    }

    // Legacy simple model (backward compatible).
    const f32 dist = std::abs(layer_world_z - dof.focus_z);
    f32 blur = dist * dof.aperture;
    // Per-side bokeh clamp for legacy model too.
    const bool is_near = layer_world_z > dof.focus_z;  // higher Z = closer to camera
    const f32 bokeh_limit = (is_near && dof.near_bokeh_radius > 0.0f) ? dof.near_bokeh_radius
                          : (!is_near && dof.far_bokeh_radius > 0.0f)  ? dof.far_bokeh_radius
                          : dof.max_blur;
    return std::clamp(blur, 0.0f, bokeh_limit);
}

} // namespace chronon3d
