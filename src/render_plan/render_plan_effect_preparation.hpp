#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <vector>

namespace chronon3d::render_plan::detail {
[[nodiscard]] std::vector<std::vector<EffectParams>> prepare_layer_effects(
    const RenderPlan& plan, chronon3d::assets::AssetResolver& resolver);
}
