#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {

inline chronon3d::Camera2_5D make_camera_value(
    const CameraPlan& source, const CanvasSpec& canvas) {
    chronon3d::Camera2_5D camera;
    camera.enabled = true;
    camera.is_animated = false;
    camera.position = {source.position[0], source.position[1], source.position[2]};
    camera.rotation = {source.rotation_deg[0], source.rotation_deg[1], source.rotation_deg[2]};
    camera.fov_deg = source.fov_deg;
    camera.zoom = 1000.0f * source.zoom;
    camera.lens_shift = {source.lens_shift[0], source.lens_shift[1]};
    camera.optics_mode = source.type == "orthographic"
        ? chronon3d::CameraOpticsMode::Zoom
        : (source.focal_length_mm > 0.0f
            ? chronon3d::CameraOpticsMode::PhysicalLens
            : chronon3d::CameraOpticsMode::FieldOfView);
    if (source.type == "orthographic") camera.zoom = 1000.0f * source.zoom;
    if (source.focal_length_mm > 0.0f) {
        camera.lens.focal_length = source.focal_length_mm;
        camera.lens.sensor_width = source.sensor_width_mm;
        const float aspect = canvas.width > 0 && canvas.height > 0
            ? static_cast<float>(canvas.width) / static_cast<float>(canvas.height)
            : 16.0f / 9.0f;
        camera.lens.sensor_height = source.sensor_width_mm / aspect;
    }
    const float authored_f_stop = source.f_stop;
    if (authored_f_stop > 0.0f) camera.lens.f_stop = authored_f_stop;
    camera.dof.enabled = source.dof_enabled;
    camera.dof.focus_distance = source.dof_focus_distance;
    // The renderer's blur kernel uses a gain-like aperture internally. Derive
    // that implementation value from the single authored f-stop here.
    camera.dof.aperture = authored_f_stop > 0.0f ? 0.672f / authored_f_stop : 0.015f;
    camera.dof.max_blur = source.dof_max_blur;
    camera.dof.iris_blades = static_cast<std::uint32_t>(source.iris_blades);
    camera.dof.highlight_gain = source.dof_highlight_gain;
    camera.dof.use_physical_model = source.focal_length_mm > 0.0f && authored_f_stop > 0.0f;
    if (source.dof_enabled) {
        camera.dof.focus_z = chronon3d::world_z_plane_for_camera_space_depth(
            camera, source.dof_focus_distance);
    }
    return camera;
}

// Shared interpolation authority for both the compatibility evaluator and the
// typed program sampler. Samples are validated and ordered at the boundary.
inline chronon3d::Camera2_5D sample_resolved_camera(
    const std::vector<std::pair<double, chronon3d::Camera2_5D>>& samples,
    double frame) {
    const auto upper = std::lower_bound(samples.begin(), samples.end(), frame,
        [](const auto& sample, double value) { return sample.first < value; });
    if (upper == samples.begin()) return upper->second;
    if (upper == samples.end()) return samples.back().second;
    if (upper->first == frame) return upper->second;
    const auto& right = upper->second;
    const auto& left = std::prev(upper)->second;
    const double span = upper->first - std::prev(upper)->first;
    const float t = static_cast<float>((frame - std::prev(upper)->first) / span);
    auto camera = left;
    camera.position = glm::mix(left.position, right.position, t);
    camera.rotation = math::camera_rotation_euler(
        glm::slerp(left.rotation_quaternion(), right.rotation_quaternion(), t));
    camera.fov_deg = glm::mix(left.fov_deg, right.fov_deg, t);
    camera.zoom = glm::mix(left.zoom, right.zoom, t);
    camera.lens_shift = glm::mix(left.lens_shift, right.lens_shift, t);
    camera.lens.focal_length = glm::mix(left.lens.focal_length, right.lens.focal_length, t);
    camera.lens.f_stop = glm::mix(left.lens.f_stop, right.lens.f_stop, t);
    camera.lens.sensor_width = glm::mix(left.lens.sensor_width, right.lens.sensor_width, t);
    camera.lens.sensor_height = glm::mix(left.lens.sensor_height, right.lens.sensor_height, t);
    camera.dof.focus_distance = glm::mix(left.dof.focus_distance, right.dof.focus_distance, t);
    camera.dof.aperture = glm::mix(left.dof.aperture, right.dof.aperture, t);
    camera.dof.max_blur = glm::mix(left.dof.max_blur, right.dof.max_blur, t);
    camera.dof.highlight_gain = glm::mix(left.dof.highlight_gain, right.dof.highlight_gain, t);
    camera.is_animated = true;
    if (camera.dof.enabled && !camera.dof.use_physical_model)
        camera.dof.focus_z = chronon3d::world_z_plane_for_camera_space_depth(
            camera, camera.dof.focus_distance);
    return camera;
}

} // namespace chronon3d::render_plan::detail
