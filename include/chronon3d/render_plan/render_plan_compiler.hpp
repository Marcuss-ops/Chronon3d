#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <memory>

namespace chronon3d::render_plan {

Result<std::shared_ptr<const Composition>, PlanDecodeError>
compile_render_plan(const RenderPlan& plan,
                    const chronon3d::assets::AssetResolver& resolver);

}  // namespace chronon3d::render_plan
