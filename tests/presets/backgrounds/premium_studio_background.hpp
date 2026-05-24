#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::presets::backgrounds {

enum class StudioMood {
    Cyber,
    Minimal,
    Luxury
};

struct PremiumStudioBackgroundParams {
    StudioMood mood{StudioMood::Cyber};
    bool particles{true};
    bool camera_motion{true};
};

void premium_studio_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const PremiumStudioBackgroundParams& params = {}
);

} // namespace chronon3d::presets::backgrounds
