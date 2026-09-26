#pragma once

#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/core/temporal_curve_1d.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <optional>
#include <string_view>

namespace chronon3d::render_plan::detail {

[[nodiscard]] chronon3d::FitMode fit_mode(FitMode value);
[[nodiscard]] chronon3d::EasingCurve animation_keyframe_easing(
    const AnimationTrackPlan& track, const AnimationKeyframePlan& key);
[[nodiscard]] std::optional<chronon3d::camera_v1::CameraDescriptor>
make_camera_descriptor(const RenderPlan& plan);
[[nodiscard]] chronon3d::DepthRole depth_role_value(std::string_view value);
void apply_text_animators(chronon3d::TextRunBuilder& builder, const LayerPlan& layer);
void apply_text_glow(chronon3d::LayerBuilder& builder, const LayerPlan& layer);
[[nodiscard]] chronon3d::TextDefinition materialize_text(
    const LayerPlan& layer, const chronon3d::CanvasInfo& canvas);
void apply_layer_primitives(chronon3d::LayerBuilder& builder,
                            const LayerPlan& layer,
                            int canvas_width,
                            int canvas_height,
                            FrameRate fps);
[[nodiscard]] std::optional<chronon3d::DirectionalBlurParams>
motion_blur_params_at(const LayerPlan& layer, double frame);
[[nodiscard]] std::optional<float> stroke_dash_offset_at(const LayerPlan& layer, double frame);
[[nodiscard]] chronon3d::TemporalCurve1D make_effect_time_curve(const AnimationTrackPlan& track);

}  // namespace chronon3d::render_plan::detail
