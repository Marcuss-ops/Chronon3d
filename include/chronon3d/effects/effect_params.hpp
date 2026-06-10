#pragma once

// ── Effect parameter structs and variant type ──────────────────────────────
// Separated from effect_stack.hpp to break the circular dependency between
// EffectInstance (needs variant) and the param struct definitions.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <variant>
#include <vector>

namespace chronon3d {

struct BlurParams          { f32   radius{0.0f}; };
struct TintParams          { Color color{0,0,0,0}; f32 amount{1.0f}; };
struct BrightnessParams    { f32   value{0.0f}; };
struct ContrastParams      { f32   value{1.0f}; };
struct DropShadowParams    { Vec2 offset{0,8}; Color color{0,0,0,0.35f}; f32 radius{12}; };

struct GlowLayer {
    f32 radius{0.0f};
    f32 opacity{1.0f};
    f32 scale{1.0f};
};

enum class GlowQuality {
    Standard,
    SkiaLike
};

struct GlowParams {
    f32 radius{15.0f};
    f32 intensity{0.8f};
    Color color{1,1,1,1};
    f32 threshold{0.0f};
    f32 spread{1.0f};
    f32 softness{1.0f};
    f32 falloff{0.85f};
    f32 core_strength{0.70f};
    f32 aura_strength{0.35f};
    f32 bloom_strength{0.18f};
    f32 outer_downscale{0.25f};
    bool preserve_source{true};
    bool additive{true};

    GlowQuality quality{GlowQuality::Standard};
    std::vector<GlowLayer> layers;
    BlendMode blend{BlendMode::Add};
};

struct BloomParams         { f32 threshold{0.80f}; f32 radius{24.0f}; f32 intensity{0.60f}; };

// ── Adjustment-layer color correction params (AE-5) ─────────────────────
struct SaturationParams    { f32 value{1.0f}; };  // 1.0 = unchanged, 0 = greyscale
struct HueRotateParams     { f32 degrees{0.0f}; }; // 0 = unchanged, 180 = invert hue
struct InvertParams        { f32 amount{1.0f}; };  // 1.0 = full invert, 0 = no-op
struct VignetteParams      {
    f32 radius{0.5f};      // 0..1, fraction of frame diagonal
    f32 softness{0.5f};    // 0..1, edge softness
    f32 amount{1.0f};      // 0..1, darkness amount
    Color color{0,0,0,1};  // vignette color (usually black)
};

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

// Type-erased variant: stores exactly one effect parameter struct, indexed by
// EffectType.  Extraction via std::get_if<T>() is O(1) with no type_info
// comparison — strictly faster than std::any_cast's runtime type check.
using EffectParams = std::variant<
    std::monostate,
    BlurParams,
    TintParams,
    BrightnessParams,
    ContrastParams,
    DropShadowParams,
    GlowParams,
    BloomParams,
    Fake3DWaveParams,
    SaturationParams,
    HueRotateParams,
    InvertParams,
    VignetteParams
>;

} // namespace chronon3d
