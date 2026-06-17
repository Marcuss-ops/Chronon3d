#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::api {

struct BackgroundOptions {
    Color background{0.01f, 0.012f, 0.025f, 1.0f};
    Color accent{0.25f, 0.45f, 1.0f, 1.0f};
    Color glow{0.20f, 0.40f, 1.0f, 0.25f};
    f32 intensity{1.0f};
    f32 speed{1.0f};
    bool animated{true};
};

} // namespace chronon3d::api
