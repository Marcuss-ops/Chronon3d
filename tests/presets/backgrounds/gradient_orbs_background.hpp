#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <vector>

namespace chronon3d::presets::backgrounds {

struct GradientOrbsBackgroundParams {
    Color background{0.02f, 0.02f, 0.05f, 1.0f};
    std::vector<Color> orb_colors{
        Color{0.15f, 0.25f, 1.0f, 0.35f},
        Color{0.5f, 0.15f, 0.8f, 0.30f},
        Color{0.1f, 0.6f, 0.7f, 0.25f}
    };
    float base_radius{320.0f};
    float speed_multiplier{1.0f};
    bool particles{true};
    bool animated{true};
};

void gradient_orbs_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const GradientOrbsBackgroundParams& params = {}
);

} // namespace chronon3d::presets::backgrounds
