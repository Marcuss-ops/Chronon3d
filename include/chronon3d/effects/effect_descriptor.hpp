#pragma once

#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_stage.hpp>
#include <string>

namespace chronon3d::effects {

struct EffectDescriptor {
    std::string   id;
    std::string   display_name;
    EffectCategory category{EffectCategory::Composite};
    EffectStage    stage{EffectStage::Composition};
    std::string   description;
    bool          builtin{false};
    bool          temporal{false};
};

} // namespace chronon3d::effects
