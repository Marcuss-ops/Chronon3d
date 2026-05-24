#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::presets::backgrounds {

struct DataStreamBackgroundParams {
    Color background{0.005f, 0.008f, 0.015f, 1.0f};
    Color stream_color{0.2f, 0.8f, 1.0f, 0.25f};
    int density{30};
    float speed{2.5f};
    float glow_radius{8.0f};
    bool animated{true};
};

void data_stream_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const DataStreamBackgroundParams& params = {}
);

} // namespace chronon3d::presets::backgrounds
