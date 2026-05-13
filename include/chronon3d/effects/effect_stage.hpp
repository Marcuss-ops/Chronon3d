#pragma once

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

[[nodiscard]] constexpr std::string_view to_string(EffectStage stage) {
    switch (stage) {
    case EffectStage::Node:               return "node";
    case EffectStage::LayerPreTransform:  return "layer_pre_transform";
    case EffectStage::LayerPostTransform: return "layer_post_transform";
    case EffectStage::Adjustment:         return "adjustment";
    case EffectStage::Composition:        return "composition";
    case EffectStage::Temporal:           return "temporal";
    }
    return "composition";
}

} // namespace chronon3d::effects
