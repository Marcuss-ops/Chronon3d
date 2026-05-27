#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <string_view>

namespace chronon3d::effects {

enum class EffectStage {
    Node,
    LayerPreTransform,
    LayerPostTransform,
    Adjustment,
    Composition,
    Temporal,
};

[[nodiscard]] inline std::string to_string(EffectStage stage) {
    return enum_utils::enum_name_lower_snake(stage);
}

} // namespace chronon3d::effects
