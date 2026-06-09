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

// ── Adjustment-layer color correction effects (AE-5) ──
inline constexpr std::string_view ColorSaturation = "color.saturation";
inline constexpr std::string_view ColorHueRotate   = "color.hue_rotate";
inline constexpr std::string_view ColorInvert      = "color.invert";
inline constexpr std::string_view ColorVignette    = "color.vignette";
inline constexpr std::string_view FocusInLadder    = "blur.focus_in_ladder";

} // namespace chronon3d::effects::ids
