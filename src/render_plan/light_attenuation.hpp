#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::render_plan {

/// Deterministic radial attenuation for point and spot lights. A zero range
/// uses the legacy radius; a positive range overrides it. Distance at or past
/// the light boundary contributes no illumination.
[[nodiscard]] inline float light_range_attenuation(
    const LightLayerPlan& light, float distance) noexcept {
    if (!std::isfinite(distance) || distance < 0.0f) return 0.0f;
    const float range = light.range > 0.0f ? light.range : light.radius;
    if (!std::isfinite(range) || range <= 0.0f || distance >= range) return 0.0f;
    const float normalized = std::clamp(distance / range, 0.0f, 1.0f);
    return std::pow(1.0f - normalized, light.decay);
}

} // namespace chronon3d::render_plan
