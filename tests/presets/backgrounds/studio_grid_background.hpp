#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::presets::backgrounds {

struct StudioGridBackgroundParams {
    Color background{0.01f, 0.012f, 0.025f, 1.0f};
    Color grid_color{0.25f, 0.45f, 1.0f, 0.12f};
    Color glow_color{0.20f, 0.40f, 1.0f, 0.25f};

    float spacing{120.0f};
    float line_thickness{1.0f};
    float glow_radius{120.0f};
    float camera_drift{40.0f};
    bool animated{true};
    bool centered{true};
};

void studio_grid_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const StudioGridBackgroundParams& params = {}
);

} // namespace chronon3d::presets::backgrounds
