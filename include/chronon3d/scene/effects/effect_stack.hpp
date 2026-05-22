#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/effects/effect_instance.hpp>
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

enum class WaveAxis {
    Horizontal,
    Vertical,
};

struct Fake3DWaveParams {
    f32 amplitude_px{18.0f};
    f32 frequency{2.2f};
    f32 speed{3.5f};
    f32 depth_px{35.0f};
    f32 phase{0.0f};
    i32 slices{24};
    WaveAxis axis{WaveAxis::Horizontal};
    f32 perspective{0.06f};
    f32 highlight{0.20f};
    f32 side_darkening{0.18f};
    bool shadow_enabled{true};
    Color shadow_color{1.0f, 0.05f, 0.05f, 0.75f};
    Vec2 shadow_offset{10.0f, 8.0f};
    f32 shadow_blur{0.0f};
    bool expand_bounds{true};
};

// Unify with the modular effects system
using EffectInstance = effects::EffectInstance;
using EffectStack = std::vector<EffectInstance>;

} // namespace chronon3d
