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

} // namespace chronon3d::effects::ids
