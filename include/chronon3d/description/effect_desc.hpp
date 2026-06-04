#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
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

struct SaturationEffectDesc {
    f32 value{1.0f};  // 1.0 = unchanged, 0 = greyscale
};

struct HueRotateEffectDesc {
    f32 degrees{0.0f};
};

struct InvertEffectDesc {
    f32 amount{1.0f}; // 1.0 = full invert
};

struct VignetteEffectDesc {
    f32 radius{0.5f};
    f32 softness{0.5f};
    f32 amount{1.0f};
};

using EffectDesc = std::variant<
    BlurEffectDesc,
    TintEffectDesc,
    BrightnessContrastEffectDesc,
    DropShadowEffectDesc,
    GlowEffectDesc,
    SaturationEffectDesc,
    HueRotateEffectDesc,
    InvertEffectDesc,
    VignetteEffectDesc
>;

} // namespace chronon3d
