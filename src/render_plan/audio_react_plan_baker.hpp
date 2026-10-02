#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

namespace chronon3d::render_plan::detail {
void bake_audio_react_plan(RenderPlan& plan, chronon3d::assets::AssetResolver& resolver);
}
