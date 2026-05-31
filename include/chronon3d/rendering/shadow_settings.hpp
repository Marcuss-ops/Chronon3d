// include/chronon3d/rendering/shadow_settings.hpp
#pragma once
#include <chronon3d/core/types/types.hpp>

namespace chronon3d::rendering {

struct ShadowSettings {
    f32 opacity{0.35f};       // shadow alpha multiplier
    f32 blur_radius{8.0f};    // Gaussian blur radius in pixels
    f32 px_per_unit{1.0f};    // world units → screen pixels (explicit V1, not derived)
    f32 max_offset{300.0f};   // screen-space offset clamp
    f32 contact_opacity{0.28f};
    f32 contact_blur_radius{10.0f};
    f32 ambient_opacity{0.08f};
    f32 ambient_blur_radius{96.0f};
};

} // namespace chronon3d::rendering
