#pragma once

#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_stage.hpp>
#include <string>
#include <memory>
#include <functional>

namespace chronon3d {
    namespace graph { class RenderGraphNode; }
}

namespace chronon3d::effects {

struct EffectInstance;

using EffectNodeFactory = std::function<std::unique_ptr<chronon3d::graph::RenderGraphNode>(const EffectInstance&)>;

struct EffectDescriptor {
    std::string   id;
    std::string   display_name;
    EffectCategory category{EffectCategory::Composite};
    EffectStage    stage{EffectStage::Composition};
    std::string   description;
    bool          builtin{false};
    bool          temporal{false};
    
    EffectNodeFactory factory;
};

} // namespace chronon3d::effects
