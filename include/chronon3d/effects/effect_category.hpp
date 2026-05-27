#pragma once

#include <chronon3d/core/enum_utils.hpp>
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

[[nodiscard]] inline std::string to_string(EffectCategory category) {
    return enum_utils::enum_name_lower_snake(category);
}

} // namespace chronon3d::effects
