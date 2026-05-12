#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/core/types.hpp>
#include <variant>

namespace chronon3d {

struct BlurEffectDesc {
    f32 radius{0.0f};
};

struct TintEffectDesc {
    Color color{0.0f, 0.0f, 0.0f, 0.0f};
};

struct BrightnessContrastEffectDesc {
    f32 brightness{0.0f};  // 0 = unchanged
    f32 contrast{1.0f};    // 1 = unchanged
};

struct DropShadowEffectDesc {
    Vec2  offset{0.0f, 8.0f};
    Color color{0.0f, 0.0f, 0.0f, 0.35f};
    f32   radius{12.0f};
};

struct GlowEffectDesc {
    f32   radius{15.0f};
    f32   intensity{0.8f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

using EffectDesc = std::variant<
    BlurEffectDesc,
    TintEffectDesc,
    BrightnessContrastEffectDesc,
    DropShadowEffectDesc,
    GlowEffectDesc
>;

} // namespace chronon3d
