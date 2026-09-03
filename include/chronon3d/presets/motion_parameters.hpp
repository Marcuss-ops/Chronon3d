#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <optional>

namespace chronon3d::presets {

// Optional parameters shared by all built-in motion descriptors.  A missing
// field means "use the descriptor's canonical default"; this keeps one
// catalog entry capable of expressing both preset defaults and authored
// parameter overrides without growing LayerBuilder methods.
struct MotionParameters {
    std::optional<Vec3> vector;
    std::optional<f32> amount;
    std::optional<f32> scale;
    std::optional<Frame> duration;
    std::optional<Frame> delay;
    std::optional<Frame> cycle;
    std::optional<EasingCurve> easing;
};

} // namespace chronon3d::presets

