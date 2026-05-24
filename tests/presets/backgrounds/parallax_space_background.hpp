#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::presets::backgrounds {

struct ParallaxSpaceBackgroundParams {
    Color background{0.02f, 0.02f, 0.04f, 1.0f};
    int card_count{8};
    float depth_span{800.0f}; // distance from foreground to background
    bool dof_enabled{true};
    float focus_z{0.0f};
    float max_blur{12.0f};
    bool animated{true};
};

void parallax_space_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const ParallaxSpaceBackgroundParams& params = {}
);

} // namespace chronon3d::presets::backgrounds
