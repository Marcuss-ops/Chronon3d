#pragma once

#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

namespace chronon3d::render_plan::detail {

[[nodiscard]] chronon3d::FitMode fit_mode(FitMode value);
void apply_text_animators(chronon3d::TextRunBuilder& builder, const LayerPlan& layer);
[[nodiscard]] chronon3d::TextDefinition materialize_text(
    const LayerPlan& layer, const chronon3d::CanvasInfo& canvas);
[[nodiscard]] RenderJobFingerprint render_job_fingerprint(
    const RenderPlan& plan,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const RenderPlanFingerprintOptions& options);
void apply_layer_primitives(chronon3d::LayerBuilder& builder, const LayerPlan& layer);

}  // namespace chronon3d::render_plan::detail
