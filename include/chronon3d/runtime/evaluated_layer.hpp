#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/depth_role.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/description/visual_desc.hpp>
#include <string>
#include <vector>

namespace chronon3d {

struct EvaluatedLayer {
    std::string name;

    bool      visible{true};
    Transform world_transform;
    f32       opacity{1.0f};

    bool      is_3d{false};
    DepthRole depth_role{DepthRole::None};
    f32       resolved_z{0.0f};

    BlendMode blend_mode{BlendMode::Normal};

    std::vector<VisualDesc> visuals;

    EffectStack resolved_effects;
};

} // namespace chronon3d
