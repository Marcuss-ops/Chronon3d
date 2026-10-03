#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include "effect_kind_coverage.hpp"

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::render_plan::detail {

[[nodiscard]] bool invalid_logical_path(const std::string& value);
[[nodiscard]] bool is_3d_property(const std::string& property);
[[nodiscard]] std::optional<std::string> find_3d_track(const nlohmann::json& layer);
[[nodiscard]] std::optional<PlanDecodeError> apply_layout_stagger(
    std::vector<LayerPlan>& layers, int canvas_width, int canvas_height);
void collect_final_key_timing_warnings(
    const nlohmann::json& value, std::vector<std::string>& warnings,
    bool keyframes = false);
void validate_per_key_timing(const nlohmann::json& value,
                             bool keyframes = false,
                             bool scalar_audio_gain = false);
[[nodiscard]] graphics::GradientDefinition decode_gradient_definition(
    const nlohmann::json& raw_gradient);
[[nodiscard]] graphics::Field2D decode_field2d_plan(const nlohmann::json& raw_field);
[[nodiscard]] std::optional<std::array<float, 4>> decode_bezier_curve(
    const nlohmann::json& object, std::string_view easing, std::string_view field);
[[nodiscard]] AnimationTrackPlan decode_animation_track(const nlohmann::json& value);
[[nodiscard]] TransitionPlan decode_transition(const nlohmann::json& value);
void decode_shape_and_fill_gradient(LayerPlan& layer, const nlohmann::json& value);
void decode_layer_masks(LayerPlan& layer, const nlohmann::json& value);
void decode_layer_text_animators(LayerPlan& layer, const nlohmann::json& value);
[[nodiscard]] std::string_view effect_id_for_plan_kind(LayerPlan::EffectKindPlan kind);
void decode_layer_effects(LayerPlan& layer, const nlohmann::json& value);
[[nodiscard]] LayerType layer_type(const std::string& value);
[[nodiscard]] FitMode fit_mode(const std::string& value);
[[nodiscard]] std::optional<chronon3d::BlendMode> blend_mode(const std::string& value);
[[nodiscard]] OutputFormat output_format(const std::string& value);
[[nodiscard]] VideoCodec video_codec(const std::string& value);
[[nodiscard]] CameraPropertyPlan camera_property(const std::string& value);
[[nodiscard]] CameraPlan decode_camera(const nlohmann::json& value);
[[nodiscard]] std::vector<CameraTrackPlan> decode_camera_tracks(const nlohmann::json& value);
[[nodiscard]] ShapePathCommandPlan decode_shape_path_command(
    const nlohmann::json& value, std::string_view context);
void decode_layer_inline_mesh(LayerPlan&, const nlohmann::json&);
void decode_layer_text_spans(LayerPlan&, const nlohmann::json&);
[[nodiscard]] std::optional<Color> decode_text_span_color(const std::string& encoded);
[[nodiscard]] LayerPlan::StaggerPlan decode_stagger(const nlohmann::json& raw);
[[nodiscard]] std::vector<ShapePathCommandPlan> decode_follow_path_commands(const nlohmann::json& raw);
void decode_background_look(LayerPlan&, const nlohmann::json&);
void decode_effect_parameter_tracks(LayerPlan&, const nlohmann::json&);
void decode_layer_layout(LayerPlan&, const nlohmann::json&);
[[nodiscard]] std::optional<Frame> optional_frame(const nlohmann::json&, const char*);
[[nodiscard]] LayerPlan decode_layer(const nlohmann::json& value);

}  // namespace chronon3d::render_plan::detail
