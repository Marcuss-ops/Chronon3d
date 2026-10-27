#pragma once

#include <array>

namespace chronon3d::render_plan {

struct LightingRigPlan {
    std::array<float, 3> key_direction{-0.2857143f, 0.8571429f, -0.4285714f};
    std::array<float, 4> key_color{1.0f, 1.0f, 1.0f, 1.0f};
    float key_intensity{0.80f};
    std::array<float, 4> ambient_color{0.6f, 0.65f, 0.7f, 1.0f};
    float ambient_intensity{0.25f};
    bool rim_enabled{false};
    std::array<float, 4> rim_color{1.0f, 1.0f, 1.0f, 1.0f};
    float rim_intensity{0.30f};
    float rim_power{2.2f};
};


} // namespace chronon3d::render_plan
