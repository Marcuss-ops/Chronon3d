#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace chronon3d::render_plan {

using ResolvedTransformSample = LayerPlan::ResolvedTransformSample;

[[nodiscard]] inline std::optional<std::string> transform_samples_problem(const LayerPlan& layer) {
    if (layer.transform_samples.empty()) return std::nullopt;
    if (layer.transform_samples.size() > 250'000 || layer.parent || layer.animation || layer.follow_path || layer.auto_orient ||
        layer.transition_in || layer.transition_out)
        return "resolved transform samples require a bounded flattened layer without animation or follow_path";
    if (layer.type != LayerType::Image && layer.type != LayerType::Color && layer.type != LayerType::Text && layer.type != LayerType::Shape)
        return "resolved transform samples support image, color, text and shape layers only";
    double previous = -1'000'001.;
    for (const auto& sample : layer.transform_samples) {
        const auto finite = [](const auto& values) {
            return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v) && std::abs(v) <= 1'000'000.f; });
        };
        if (!std::isfinite(sample.frame) || sample.frame <= previous || sample.frame < -1'000'000. || sample.frame > 1'000'000. ||
            !finite(sample.position) || !finite(sample.scale) || !finite(sample.rotation_deg) ||
            !std::isfinite(sample.opacity) || sample.opacity < 0.f || sample.opacity > 1.f)
            return "resolved transform samples require increasing bounded times and finite TRS/opacity";
        previous = sample.frame;
    }
    return std::nullopt;
}

/// Same interpolation for the typed consumer and compatibility scene. Exact
/// authored timestamps return the stored value without conversion or rounding.
[[nodiscard]] inline ResolvedTransformSample sample_resolved_transform(
    const std::vector<ResolvedTransformSample>& samples, double frame) {
    const auto upper = std::lower_bound(samples.begin(), samples.end(), frame,
        [](const auto& sample, double time) { return sample.frame < time; });
    if (upper == samples.begin()) return *upper;
    if (upper == samples.end()) return samples.back();
    if (upper->frame == frame) return *upper;
    const auto& left = *std::prev(upper);
    const auto& right = *upper;
    const float t = static_cast<float>((frame - left.frame) / (right.frame - left.frame));
    auto result = left;
    result.frame = frame;
    for (std::size_t i = 0; i < 3; ++i) {
        result.position[i] = glm::mix(left.position[i], right.position[i], t);
        result.scale[i] = glm::mix(left.scale[i], right.scale[i], t);
    }
    const auto rotation = math::camera_rotation_euler(glm::slerp(
        glm::quat(glm::radians(Vec3{left.rotation_deg[0], left.rotation_deg[1], left.rotation_deg[2]})),
        glm::quat(glm::radians(Vec3{right.rotation_deg[0], right.rotation_deg[1], right.rotation_deg[2]})), t));
    result.rotation_deg = {rotation.x, rotation.y, rotation.z};
    result.opacity = glm::mix(left.opacity, right.opacity, t);
    return result;
}

} // namespace chronon3d::render_plan
