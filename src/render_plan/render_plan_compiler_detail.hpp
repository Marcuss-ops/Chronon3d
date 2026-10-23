#pragma once

#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>
namespace chronon3d { class FrameContext; }
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/core/temporal_curve_1d.hpp>
#include <optional>
#include <memory>
#include <vector>
#include <string>
#include <chronon3d/scene/model/core/scene.hpp>
#include <functional>
#include <filesystem>
#include <string_view>
#include <utility>

namespace chronon3d::render_plan::detail {

void materialize_media_background_cards(RenderPlan& plan);
[[nodiscard]] std::optional<PlanDecodeError> prepare_data_feed_assets(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver);
[[nodiscard]] std::optional<PlanDecodeError> expand_lottie_assets(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver);
[[nodiscard]] std::optional<PlanDecodeError> prepare_svg_layers(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver);

struct SvgTextMetrics {
    std::string font_asset;
    float advance{0.0f};
    float ink_width{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
    float x_height{0.0f};
    float cap_height{0.0f};
    float line_height{0.0f};
    std::optional<float> script_baseline_offset;
    std::optional<float> central_baseline_offset;
    std::optional<float> hanging_baseline_offset;
    std::optional<float> ideographic_baseline_offset;
    std::optional<float> mathematical_baseline_offset;
    std::optional<float> before_edge_baseline_offset;
    std::optional<float> after_edge_baseline_offset;
};

[[nodiscard]] std::optional<PlanDecodeError> append_merged_svg_text_layers(
    RenderPlan& plan,
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>>& pending_svg_text_layers,
    const std::vector<SvgTextMetrics>& pending_svg_text_metrics,
    const std::vector<bool>& pending_svg_text_boundary_before);

void append_shape_layer(chronon3d::LayerBuilder& builder,
                        const LayerPlan& layer, const RenderPlan& plan,
                        const chronon3d::FrameContext& ctx);

struct FollowPathEvaluators {
    std::function<chronon3d::Vec3(double)> position;
    std::function<chronon3d::Vec3(double)> rotation;
};

using CameraTrackEvaluator = std::function<chronon3d::Camera2_5D(double)>;

struct ResolvedLayerMediaAssets {
    std::vector<std::string> video_paths;
    std::vector<std::string> particle_sprite_paths;
};

[[nodiscard]] ResolvedLayerMediaAssets resolve_layer_media_assets(
    const RenderPlan& plan, chronon3d::assets::AssetResolver& resolver);

struct PreparedTextLayers {
    std::vector<std::optional<chronon3d::PreparedText>> texts;
    std::vector<std::shared_ptr<const chronon3d::AnimatedTextDocument>> counters;
    std::vector<std::optional<chronon3d::Vec2>> bounds;
};

[[nodiscard]] PreparedTextLayers prepare_text_layers(
    const RenderPlan& plan, chronon3d::assets::AssetResolver& resolver,
    const chronon3d::CanvasInfo& canvas,
    const std::shared_ptr<chronon3d::FontEngine>& font_engine);

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
    CameraTrackEvaluator camera_evaluator;
    std::vector<FollowPathEvaluators> follow_path_evaluators;
    std::string resolved_ocio_config_path;
};
void configure_scene_camera_and_lighting(
    chronon3d::SceneBuilder&, const RenderPlan&, bool,
    const CameraTrackEvaluator&,
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
[[nodiscard]] CameraTrackEvaluator make_camera_evaluator(const RenderPlan& plan);
[[nodiscard]] CameraTrackEvaluator make_camera_evaluator(
    const RenderPlan& plan, const std::function<CameraPlan(double)>& runtime_sample);
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
