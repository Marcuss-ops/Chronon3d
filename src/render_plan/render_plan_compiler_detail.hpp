#pragma once

#include <chronon3d/render_plan/render_plan_compiler.hpp>
namespace chronon3d { class FrameContext; }
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
#include <memory>
#include <vector>
#include <string>
#include <chronon3d/scene/model/core/scene.hpp>
#include <functional>
#include <filesystem>
#include <string_view>

namespace chronon3d::render_plan::detail {

void append_shape_layer(chronon3d::LayerBuilder& builder,
                        const LayerPlan& layer, const RenderPlan& plan,
                        const chronon3d::FrameContext& ctx);

struct FollowPathEvaluators {
    std::function<chronon3d::Vec3(double)> position;
    std::function<chronon3d::Vec3(double)> rotation;
};

struct SceneMaterializationInputs {
    RenderPlan plan;
    std::vector<std::string> resolved_video_paths;
    std::vector<std::string> resolved_particle_sprite_paths;
    std::vector<std::optional<chronon3d::PreparedText>> prepared_texts;
    std::vector<std::shared_ptr<const chronon3d::AnimatedTextDocument>> prepared_text_counters;
    std::vector<std::optional<chronon3d::Vec2>> prepared_text_bounds;
    std::vector<std::vector<chronon3d::EffectParams>> prepared_effects;
    std::shared_ptr<chronon3d::FontEngine> font_engine;
    bool has_3d_layer{};
    std::optional<chronon3d::camera_v1::CameraDescriptor> plan_camera;
    std::vector<FollowPathEvaluators> follow_path_evaluators;
    std::string resolved_ocio_config_path;
};
void configure_scene_camera_and_lighting(
    chronon3d::SceneBuilder&, const RenderPlan&, bool,
    const std::optional<chronon3d::camera_v1::CameraDescriptor>&,
    const chronon3d::FrameContext&);

[[nodiscard]] std::function<chronon3d::Scene(const chronon3d::FrameContext&)>
make_scene_evaluator(SceneMaterializationInputs inputs);

[[nodiscard]] FollowPathEvaluators make_follow_path_evaluators(
    const LayerPlan::FollowPathDriverPlan& follow,
    chronon3d::Vec3 base_position,
    chronon3d::Vec3 base_rotation,
    int canvas_width,
    int canvas_height,
    FrameRate fps);

[[nodiscard]] chronon3d::FitMode fit_mode(FitMode value);
[[nodiscard]] chronon3d::LayerTransitionSpec compile_transition(
    const TransitionPlan& plan, FrameRate fps);
[[nodiscard]] chronon3d::EasingCurve animation_keyframe_easing(
    const AnimationTrackPlan& track, const AnimationKeyframePlan& key);
[[nodiscard]] std::optional<chronon3d::camera_v1::CameraDescriptor>
make_camera_descriptor(const RenderPlan& plan);
[[nodiscard]] std::optional<std::string> expand_static_lottie(
    LayerPlan& layer, const std::filesystem::path& path);
[[nodiscard]] chronon3d::DepthRole depth_role_value(std::string_view value);
void apply_text_animators(chronon3d::TextRunBuilder& builder, const LayerPlan& layer, FrameRate fps);
void apply_text_glow(chronon3d::LayerBuilder& builder, const LayerPlan& layer);
[[nodiscard]] chronon3d::TextDefinition materialize_text(
    const LayerPlan& layer, const chronon3d::CanvasInfo& canvas);
void apply_layer_primitives(chronon3d::LayerBuilder& builder,
                            const LayerPlan& layer,
                            int canvas_width,
                            int canvas_height,
                            FrameRate fps,
                            const FollowPathEvaluators* precompiled_follow_path = nullptr);
[[nodiscard]] std::optional<chronon3d::DirectionalBlurParams>
motion_blur_params_at(const LayerPlan& layer, double frame, FrameRate fps,
                      const FollowPathEvaluators* precompiled_follow_path = nullptr);
[[nodiscard]] chronon3d::Vec3 light_position_at(
    const LayerPlan& layer, double frame, chronon3d::Vec3 base, FrameRate fps);
[[nodiscard]] chronon3d::Vec3 light_direction_at(
    const LayerPlan& layer, double frame, FrameRate fps);
[[nodiscard]] float evaluate_scalar_track(
    const AnimationTrackPlan& track, double frame, float base, FrameRate fps);
[[nodiscard]] std::optional<float> stroke_dash_offset_at(
    const LayerPlan& layer, double frame, FrameRate fps);
[[nodiscard]] std::optional<float> stroke_width_at(
    const LayerPlan& layer, double frame, float base, FrameRate fps);
[[nodiscard]] std::optional<chronon3d::Color> stroke_color_at(
    const LayerPlan& layer, double frame, chronon3d::Color base, FrameRate fps);
[[nodiscard]] std::optional<float> shadow_scalar_at(
    const LayerPlan& layer, std::string_view property, double frame,
    float base, FrameRate fps);
[[nodiscard]] std::optional<chronon3d::Color> shadow_color_at(
    const LayerPlan& layer, std::string_view property, double frame,
    chronon3d::Color base, FrameRate fps);
[[nodiscard]] std::optional<chronon3d::Color> fill_color_at(
    const LayerPlan& layer, double frame, chronon3d::Color base, FrameRate fps);
[[nodiscard]] std::optional<graphics::GradientDefinition> fill_gradient_at(
    const LayerPlan& layer, double frame);
[[nodiscard]] graphics::GradientDefinition gradient_animation_at(
    const graphics::GradientDefinition& base,
    const GradientAnimationPlan& track, double frame);
[[nodiscard]] chronon3d::TemporalCurve1D make_effect_time_curve(const AnimationTrackPlan& track);

}  // namespace chronon3d::render_plan::detail
