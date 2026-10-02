#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

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

/// Camera-space depth where the optical axis intersects the legacy world-Z
/// focus plane. `focus_z` remains a world-Z coordinate for compatibility.
/// A tilted camera sees different depths across that plane; this central-axis
/// intersection is the single scalar reference used by the legacy model.
[[nodiscard]] inline f32 camera_space_world_z_plane_depth(
    const Camera2_5DRuntime& camera,
    f32 world_z) {
    const Mat4 camera_to_world = glm::inverse(camera.view_matrix());
    const Vec3 forward = glm::normalize(Vec3{camera_to_world[2]});
    if (std::abs(forward.z) > 1e-6f) {
        const f32 distance = (world_z - camera.position.z) / forward.z;
        const Vec3 axis_intersection = camera.position + forward * distance;
        return (camera.view_matrix() * Vec4{axis_intersection, 1.0f}).z;
    }

    // The optical axis is parallel to the authored world-Z plane, so there is
    // no finite intersection. Preserve a deterministic axis-distance fallback.
    return (camera.view_matrix() * Vec4{0.0f, 0.0f, world_z, 1.0f}).z;
}

/// Convert the authored legacy world-Z focus plane to the positive
/// camera-space reference depth used by the render graph.
[[nodiscard]] inline DepthOfFieldSettings camera_space_dof_settings(
    const DepthOfFieldSettings& dof,
    const Camera2_5DRuntime& camera) {
    DepthOfFieldSettings result = dof;
    if (!dof.use_physical_model) {
        result.focus_z = camera_space_world_z_plane_depth(camera, dof.focus_z);
    }
    return result;
}

// Camera-space depth at an authored world point. Rotated world-Z planes are
// not constant-depth planes, so per-pixel sources use this rather than the
// scalar compatibility focus-plane intersection.
[[nodiscard]] inline f32 camera_space_depth(
    const Camera2_5DRuntime& camera,
    const Vec3& world_position) {
    return (camera.view_matrix() * Vec4{world_position, 1.0f}).z;
}

/// Compute the DoF blur radius in pixels for a positive camera-space depth.
///
/// When `use_physical_model` is true, uses the thin-lens Circle of Confusion
/// model. Otherwise falls back to the legacy linear formula. For that legacy
/// branch, callers should first pass settings through `camera_space_dof_settings`
/// so focus_z denotes the matching camera-space focus plane.
///
/// @param viewport_width Used to scale the physical CoC from sensor-mm to px.
///                       Defaults to 1920 (common HD width).
[[nodiscard]] inline f32 compute_dof_blur_radius(
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    f32 camera_depth,
    f32 viewport_width = 1920.0f
) {
    if (!dof.enabled || !std::isfinite(camera_depth) || camera_depth <= 0.0f) {
        return 0.0f;
    }

    if (dof.use_physical_model) {
        const f32 coc_mm = compute_physical_coc(dof, lens, camera_depth);
        if (coc_mm <= 0.0f) return 0.0f;
        // Scale CoC from sensor-mm to pixels.
        f32 pixels = coc_mm * (viewport_width / lens.sensor_width);
        // Per-side bokeh clamp: if near/far limits are set, clamp based on
        // whether the layer is nearer or farther than the focus plane.
        const bool is_near = camera_depth < dof.focus_distance;
        const f32 bokeh_limit = (is_near && dof.near_bokeh_radius > 0.0f) ? dof.near_bokeh_radius
                              : (!is_near && dof.far_bokeh_radius > 0.0f)  ? dof.far_bokeh_radius
                              : dof.max_blur;
        const f32 clamp_max = bokeh_limit > 0.0f ? bokeh_limit : 100.0f;
        return std::clamp(pixels, 0.0f, clamp_max);
    }

    // Legacy simple model. Render-graph callers first convert the authored
    // world-Z focus reference to camera-space using the runtime camera.
    const f32 dist = std::abs(camera_depth - dof.focus_z);
    f32 blur = dist * dof.aperture;
    // Per-side bokeh clamp for legacy model too. `focus_z` is converted to
    // camera-space by camera_space_dof_settings(), where smaller depth is near.
    const bool is_near = camera_depth < dof.focus_z;
    const f32 bokeh_limit = (is_near && dof.near_bokeh_radius > 0.0f) ? dof.near_bokeh_radius
                          : (!is_near && dof.far_bokeh_radius > 0.0f)  ? dof.far_bokeh_radius
                          : dof.max_blur;
    return std::clamp(blur, 0.0f, bokeh_limit);
}

} // namespace chronon3d
