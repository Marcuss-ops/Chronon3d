#pragma once

#include <string_view>

namespace chronon3d::effects::ids {

inline constexpr std::string_view BlurGaussian = "blur.gaussian";
inline constexpr std::string_view ColorTint = "color.tint";
inline constexpr std::string_view ColorBrightness = "color.brightness";
inline constexpr std::string_view ColorContrast = "color.contrast";
inline constexpr std::string_view LightDropShadow = "light.drop_shadow";
inline constexpr std::string_view LightGlow = "light.glow";
inline constexpr std::string_view LightBloom = "light.bloom";
inline constexpr std::string_view DistortFake3DWave = "distort.fake_3d_wave";

// ── Color effects: Exposure + Levels (A2) ──
inline constexpr std::string_view ColorExposure = "color.exposure";
inline constexpr std::string_view ColorLevels   = "color.levels";

// ── A4: Fill, Noise, Offset ──
inline constexpr std::string_view GenerateFill   = "generate.fill";
inline constexpr std::string_view GenerateNoise  = "generate.noise";
inline constexpr std::string_view DistortOffset  = "distort.offset";

// ── A5: Directional Blur ──
inline constexpr std::string_view DistortDirectionalBlur = "distort.directional_blur";

// ── A6: Radial Blur ──
inline constexpr std::string_view DistortRadialBlur = "distort.radial_blur";

// ── A3: Curves ──
inline constexpr std::string_view ColorCurves = "color.curves";

// ── A7: Stroke ──
inline constexpr std::string_view StylizeStroke = "stylize.stroke";

// ── Adjustment-layer color correction effects (AE-5) ──
inline constexpr std::string_view ColorSaturation = "color.saturation";
inline constexpr std::string_view ColorHueRotate   = "color.hue_rotate";
inline constexpr std::string_view ColorInvert      = "color.invert";
inline constexpr std::string_view ColorVignette    = "color.vignette";

} // namespace chronon3d::effects::ids
