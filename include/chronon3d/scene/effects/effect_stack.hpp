#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/core/types.hpp>
#include <variant>
#include <vector>

namespace chronon3d {

struct BlurParams          { f32   radius{0.0f}; };
struct TintParams          { Color color{0,0,0,0}; f32 amount{1.0f}; };
struct BrightnessParams    { f32   value{0.0f}; };
struct ContrastParams      { f32   value{1.0f}; };
struct DropShadowParams    { Vec2 offset{0,8}; Color color{0,0,0,0.35f}; f32 radius{12}; };
struct GlowParams          { f32 radius{15}; f32 intensity{0.8f}; Color color{1,1,1,1}; };
struct BloomParams         { f32 threshold{0.80f}; f32 radius{24.0f}; f32 intensity{0.60f}; };

using EffectParams = std::variant<
    BlurParams,
    TintParams,
    BrightnessParams,
    ContrastParams,
    DropShadowParams,
    GlowParams,
    BloomParams
>;

struct EffectInstance {
    EffectParams params;
    bool         enabled{true};
};

using EffectStack = std::vector<EffectInstance>;

} // namespace chronon3d
