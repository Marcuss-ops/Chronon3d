#pragma once

#include "svg_gradient_detail.hpp"
#include <chronon3d/assets/svg_path_loader.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_text_detail {
using SvgIdMap = std::map<std::string,
    std::pair<std::string, const boost::property_tree::ptree*>>;
struct SvgTextStyleState {
    std::string font_family{"serif"};
    float font_size{16.0f};
    int font_weight{400};
    std::string font_style{"normal"};
    std::string direction{"auto"};
    std::string text_anchor{"start"};
    std::string dominant_baseline{"auto"};
    float baseline_shift{0.0f};
    std::optional<std::string> text_path_id;
    float text_path_start_offset{0.0f};
    bool text_path_start_offset_percent{false};
    std::optional<PathShape> text_path_geometry;
    Color fill{Color::black()};
    std::optional<graphics::GradientDefinition> fill_gradient;
    std::shared_ptr<const graphics::PatternTile> fill_pattern;
    std::optional<Color> stroke;
    std::optional<graphics::GradientDefinition> stroke_gradient;
    std::shared_ptr<const graphics::PatternTile> stroke_pattern;
    float stroke_width{1.0f};
    LineCap stroke_cap{LineCap::Butt};
    LineJoin stroke_join{LineJoin::Miter};
    float stroke_miter_limit{4.0f};
    std::vector<float> stroke_dash_array;
    float stroke_dash_offset{0.0f};
    Color current_color{Color::black()};
    float opacity{1.0f};
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    bool display_none{false};
    bool visibility_hidden{false};
    std::optional<float> x;
    std::optional<float> y;
    float dx{0.0f};
    float dy{0.0f};
    std::vector<float> x_coordinates;
    std::vector<float> y_coordinates;
    std::vector<float> dx_coordinates;
    std::vector<float> dy_coordinates;
    std::vector<float> rotate_degrees;
    float letter_spacing{0.0f};
    float word_spacing{0.0f};
    std::optional<float> text_length;
    std::string length_adjust{"spacing"};
    std::string text_anchor_scope_id;
    std::vector<std::string> group_ids;
    std::vector<std::string> coordinate_scope_ids;
};
[[nodiscard]] std::optional<float> svg_baseline_shift(std::string value, float font_size);
[[nodiscard]] std::optional<std::string> svg_text_path_reference(
    const boost::property_tree::ptree& node);
[[nodiscard]] std::optional<std::pair<float, bool>> svg_text_path_offset(
    const boost::property_tree::ptree& node);
void apply_svg_text_style(const boost::property_tree::ptree& node,
                          SvgTextStyleState& state,
                          const svg_gradient_detail::SvgGradientMap& gradients);
[[nodiscard]] std::string svg_color_hex(const Color& color);
void collect_svg_text_runs(
    const SvgIdMap& ids,
    const boost::property_tree::ptree& tree,
    std::vector<SvgTextRun>& runs,
    const svg_gradient_detail::SvgGradientMap& gradients,
    const svg_gradient_detail::Affine2& parent_transform = svg_gradient_detail::Affine2(1.0f),
    SvgTextStyleState parent_style = {});
} // namespace chronon3d::assets::svg_text_detail
