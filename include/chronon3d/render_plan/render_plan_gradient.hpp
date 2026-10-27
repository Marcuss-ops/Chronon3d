#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/graphics/gradient.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::render_plan {
struct GradientKeyframePlan {
    Frame frame{0};
    graphics::GradientDefinition value;
};

struct GradientAnimationPlan {
    std::vector<GradientKeyframePlan> keyframes;
    std::string easing{"linear"};
    std::optional<std::array<float, 4>> bezier_curve;
};

}  // namespace chronon3d::render_plan
