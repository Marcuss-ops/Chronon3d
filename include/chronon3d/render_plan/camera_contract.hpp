#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace chronon3d::render_plan {

/// Shared producer/consumer admission contract. All values use RenderPlan
/// units; unsupported optics fail before rasterization on every backend.
[[nodiscard]] inline std::optional<std::string> camera_contract_problem(const CameraPlan& c) {
    const auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
    };
    if (c.type != "perspective" && c.type != "orthographic") return "camera.type must be perspective or orthographic";
    if (!finite(c.position) || !finite(c.rotation_deg)) return "camera pose must be finite";
    if (!std::isfinite(c.fov_deg) || c.fov_deg < 10.f || c.fov_deg > 120.f) return "camera.fov_deg must be in [10, 120]";
    if (!std::isfinite(c.near) || !std::isfinite(c.far) || c.near <= 0.f || c.far <= c.near) return "camera clipping planes must be finite and 0 < near < far";
    if (!std::isfinite(c.zoom) || c.zoom <= 0.f) return "camera.zoom must be finite and positive";
    if (!std::isfinite(c.sensor_width_mm) || c.sensor_width_mm < 1.f || c.sensor_width_mm > 100.f ||
        !finite(c.lens_shift) || std::abs(c.lens_shift[0]) > 1.f || std::abs(c.lens_shift[1]) > 1.f ||
        !std::isfinite(c.focal_length_mm) || (c.focal_length_mm != 0.f && (c.focal_length_mm < 12.f || c.focal_length_mm > 200.f)))
        return "camera physical lens values are outside supported bounds";
    const auto valid_stop = [](float value) { return std::isfinite(value) && (value == 0.f || (value >= .7f && value <= 32.f)); };
    if (!valid_stop(c.f_stop)) return "camera f-stop must be zero or in [0.7, 32]";
    if (!std::isfinite(c.dof_focus_distance) || c.dof_focus_distance <= 0.f || c.dof_focus_distance > 10'000'000.f ||
        !std::isfinite(c.dof_max_blur) || c.dof_max_blur < 0.f || c.dof_max_blur > 256.f)
        return "camera DoF focus/blur values are outside supported bounds";
    if (c.iris_blades != 0 || !std::isfinite(c.dof_highlight_gain) || c.dof_highlight_gain != 0.f)
        return "unsupported camera capability: iris blades and highlight gain have no renderer kernel";
    return std::nullopt;
}

} // namespace chronon3d::render_plan
