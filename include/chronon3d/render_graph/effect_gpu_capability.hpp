#pragma once

#include <chronon3d/effects/effect_type.hpp>

#include <array>
#include <string_view>

namespace chronon3d::graph {

enum class GpuEffectRoute : unsigned char {
    NativeZeroReadback,
    Unsupported,
};

struct GpuEffectCapabilityRow {
    effects::EffectType effect;
    GpuEffectRoute route;
    std::string_view diagnostic;
};

namespace gpu_capability_detail {

struct NativeRoute {
    effects::EffectType effect;
    std::string_view diagnostic;
};

// Effects with a certified native Vulkan implementation.  Everything else in
// the catalog is Unsupported and fails closed under strict native-GPU policy.
inline constexpr std::array<NativeRoute, 50> kNativeRoutes{{
    {effects::EffectType::Blur, "Vulkan separable blur dispatch"},
    {effects::EffectType::DropShadow,
     "Vulkan alpha-mask, offset, dual-blur and source-over shadow dispatch"},
    {effects::EffectType::Sharpen,
     "Vulkan separable blur plus pointwise unsharp-mask combine dispatch"},
    {effects::EffectType::Glow, "Vulkan glow dispatch"},
    {effects::EffectType::Bloom, "Vulkan bright-pass and separable bloom dispatch"},
    {effects::EffectType::Tint, "Vulkan tint dispatch"},
    {effects::EffectType::Brightness, "Vulkan color-matrix dispatch (brightness)"},
    {effects::EffectType::Contrast, "Vulkan color-matrix dispatch (contrast)"},
    {effects::EffectType::Saturation, "Vulkan color-matrix dispatch (saturation)"},
    {effects::EffectType::HueRotate, "Vulkan color-matrix dispatch (hue rotate)"},
    {effects::EffectType::Invert, "Vulkan color-matrix dispatch (invert)"},
    {effects::EffectType::Exposure, "Vulkan color-matrix dispatch (exposure)"},
    {effects::EffectType::TemperatureTint,
     "Vulkan color-matrix dispatch (temperature/tint white balance)"},
    {effects::EffectType::LiftGammaGain,
     "Vulkan color-matrix dispatch (lift/gamma/gain)"},
    {effects::EffectType::Offset,
     "Vulkan source sampling dispatch (nearest/bilinear with edge modes)"},
    {effects::EffectType::Noise,
     "Vulkan deterministic per-pixel SplitMix64 noise dispatch"},
    {effects::EffectType::Checkerboard,
     "Vulkan deterministic checkerboard generator with feathered cells"},
    {effects::EffectType::Fill, "Vulkan color-matrix dispatch (fill)"},
    {effects::EffectType::Levels, "Vulkan color-matrix dispatch (levels)"},
    {effects::EffectType::Threshold, "Vulkan color-matrix dispatch (threshold)"},
    {effects::EffectType::ChromaticAberration,
     "Vulkan inverse-sampled independent RGB channel transform"},
    {effects::EffectType::Keying, "Vulkan color-matrix dispatch (luma/chroma key)"},
    {effects::EffectType::Vignette, "Vulkan color-matrix dispatch (radial vignette)"},
    {effects::EffectType::Mosaic, "Vulkan source-sampling dispatch (integer-cell mosaic)"},
    {effects::EffectType::Echo, "Vulkan frame-addressed JobSession history and weighted source-over"},
    {effects::EffectType::DirectionalBlur,
     "Vulkan uniform box-tap blur along the authored direction"},
    {effects::EffectType::RadialBlur,
     "Vulkan zoom+spin box-tap blur around the authored center"},
    {effects::EffectType::Fake3DWave,
     "Vulkan slice-deform wave body pass (no-shadow window)"},
    {effects::EffectType::CornerPin,
     "Vulkan inverse-bilinear corner-pin resample"},
    {effects::EffectType::Twirl, "Vulkan radial falloff twirl resample"},
    {effects::EffectType::Bulge, "Vulkan radial bulge/pinch resample"},
    {effects::EffectType::MeshWarp,
     "Vulkan bilinear-grid mesh-warp resample (grids up to 3x3)"},
    {effects::EffectType::TurbulentDisplace,
     "Vulkan fBm turbulent-displace resample"},
    {effects::EffectType::WaveWarp, "Vulkan parametric wave-warp resample"},
    {effects::EffectType::Ripple, "Vulkan damped radial ripple resample"},
    {effects::EffectType::DisplacementMap,
     "Vulkan aux-image displacement-map resample"},
    {effects::EffectType::LensDistortion,
     "Vulkan radial lens-distortion resample"},
    {effects::EffectType::FoldWarp, "Vulkan crease fold-warp resample"},
    {effects::EffectType::RoughenEdges,
     "Vulkan boundary-band fBm roughen-edges resample"},
    {effects::EffectType::Stroke,
     "Vulkan box-morphology stroke composite"},
    {effects::EffectType::FractalNoise,
     "Vulkan procedural fBm fractal-noise generator"},
    {effects::EffectType::FourColorGradient,
     "Vulkan inverse-distance four-color gradient generator"},
    {effects::EffectType::LightRays,
     "Vulkan radial-integration light-rays composite"},
    {effects::EffectType::Halftone,
     "Vulkan rotated-cell halftone dot render"},
    {effects::EffectType::LuminanceResponse,
     "Vulkan threshold luminance-response composite (no-blur window)"},
    {effects::EffectType::CreaseShading,
     "Vulkan fold-crease shading multiply-add"},
    {effects::EffectType::Curves,
     "Vulkan host-baked 256-entry curves LUT"},
    {effects::EffectType::Lut3D,
     "Vulkan trilinear 3D LUT (dims up to 16)"},
    {effects::EffectType::NoiseSampler,
     "Vulkan unified multi-algorithm noise sampler (Simplex/Worley/Ridged/Value/Blue)"},
    {effects::EffectType::BandedGrain,
     "Vulkan multi-scale banded grain generator"},
}};

[[nodiscard]] constexpr GpuEffectCapabilityRow make_row(effects::EffectType effect) {
    for (const auto& native : kNativeRoutes)
        if (native.effect == effect)
            return {effect, GpuEffectRoute::NativeZeroReadback, native.diagnostic};
    return {effect, GpuEffectRoute::Unsupported,
            "Vulkan native kernel for this effect is not implemented"};
}

} // namespace gpu_capability_detail

// Canonical strict-Vulkan capability authority for the effect-stack native
// full-frame dispatches.  Rows are generated from effect_catalog.def so every
// catalog EffectType is accounted for exactly once; Unsupported rows fail
// before software fallback.
inline constexpr auto kGpuEffectCapabilityRows = std::to_array<GpuEffectCapabilityRow>({
#define CHRONON_EFFECT(val, enum_name, ...) \
    gpu_capability_detail::make_row(effects::EffectType::enum_name),
#include <chronon3d/effects/effect_catalog.def>
#undef CHRONON_EFFECT
});

[[nodiscard]] constexpr const GpuEffectCapabilityRow*
gpu_effect_capability(effects::EffectType effect) noexcept {
    for (const auto& row : kGpuEffectCapabilityRows)
        if (row.effect == effect) return &row;
    return nullptr;
}

} // namespace chronon3d::graph
