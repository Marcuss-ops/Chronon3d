#pragma once
#include <chronon3d/render_plan/render_plan.hpp>
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace chronon3d::render_plan::detail {
struct LottieSolidPaint {
    std::array<float, 4> color{};
    std::optional<AnimationTrackPlan> color_animation;
    std::optional<AnimationTrackPlan> opacity_animation;
};
struct LottiePaintSet {
    std::vector<std::optional<LottieSolidPaint>> fill_paints;
    std::vector<std::optional<graphics::GradientDefinition>> fill_gradients;
    std::vector<std::optional<StrokeStyle>> stroke_styles;
    std::vector<std::optional<AnimationTrackPlan>> stroke_color_tracks;
    std::vector<std::optional<AnimationTrackPlan>> stroke_opacity_tracks;
    std::vector<std::optional<AnimationTrackPlan>> stroke_width_tracks;
};
std::optional<std::string> decode_lottie_paints(
    const std::vector<const nlohmann::json*>& fills,
    const std::vector<const nlohmann::json*>& strokes,
    std::size_t geometry_count, LottiePaintSet& paints);
bool lottie_point(const nlohmann::json&, std::array<float, 2>&);
std::optional<std::string> decode_lottie_text_document(
    const LayerPlan&, const nlohmann::json&, const nlohmann::json&, LayerPlan&);
std::optional<std::string> decode_lottie_text_animators(
    const nlohmann::json&, LayerPlan&);
std::optional<std::string> decode_lottie_layer_effects(
    const nlohmann::json&, LayerPlan&);
std::optional<std::string> decode_lottie_masks(
    const nlohmann::json&, LayerPlan&, float anchor_x, float anchor_y);
std::optional<std::string> expand_static_lottie_layer(
    LayerPlan&, const nlohmann::json&, std::size_t);
std::optional<std::string> decode_lottie_path_operators(
    const std::vector<const nlohmann::json*>&, ShapeContentPlan&);
bool lottie_identity_transform(const nlohmann::json&);
bool lottie_static_vec2_property(const nlohmann::json&, const char*, std::array<float,2>&);
bool lottie_static_scalar_property(const nlohmann::json&, const char*, float&);
bool lottie_read_transform_property(const nlohmann::json&, const char*, std::vector<float>&, bool&);
bool lottie_decode_transform_track(const nlohmann::json&, const char*, const char*, float, AnimationTrackPlan&);
std::optional<std::string> decode_lottie_layer_transform(LayerPlan&, const nlohmann::json&, float&, float&);
}
