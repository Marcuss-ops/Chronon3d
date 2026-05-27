#pragma once

#include <chronon3d/math/math_base.hpp>

namespace chronon3d::presets::motion {

struct MotionState {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    f32 opacity{1.0f};
    f32 blur{0.0f};
    f32 text_reveal{1.0f};
    f32 mask_reveal{1.0f};
    bool visible{true};
};

} // namespace chronon3d::presets::motion
