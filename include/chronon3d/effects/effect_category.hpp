#pragma once

#include <string_view>

namespace chronon3d::effects {

enum class EffectCategory {
    Blur,
    Color,
    Light,
    Stylize,
    Distort,
    Geometry,
    Temporal,
    Composite,
};

[[nodiscard]] constexpr std::string_view to_string(EffectCategory category) {
    switch (category) {
    case EffectCategory::Blur:      return "blur";
    case EffectCategory::Color:     return "color";
    case EffectCategory::Light:     return "light";
    case EffectCategory::Stylize:   return "stylize";
    case EffectCategory::Distort:   return "distort";
    case EffectCategory::Geometry:  return "geometry";
    case EffectCategory::Temporal:   return "temporal";
    case EffectCategory::Composite: return "composite";
    }
    return "composite";
}

} // namespace chronon3d::effects
