#pragma once

#include "svg_path_loader_detail.hpp"

#include <chronon3d/assets/svg_path_loader.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::assets::svg_gradient_detail {

using Affine2 = glm::mat3;
struct SvgGradientSpec {
    bool radial{false};
    bool user_space{false};
    Vec2 p0{0.0f, 0.0f};
    Vec2 p1{1.0f, 0.0f};
    float radius{0.5f};
    Affine2 transform{1.0f};
    std::vector<graphics::GradientStop> stops;
    std::shared_ptr<const graphics::PatternTile> pattern_tile;
    struct StopAnimation {
        std::size_t stop_index{0};
        bool opacity{false};
        Color from{1, 1, 1, 1};
        Color to{1, 1, 1, 1};
        float duration_seconds{0.0f};
        bool discrete{false};
    };
    std::vector<StopAnimation> animations;
};

using SvgGradientMap = std::map<std::string, SvgGradientSpec>;

[[nodiscard]] Affine2 parse_transform(std::string_view text);
[[nodiscard]] Vec2 transform_point(const Affine2& transform, Vec2 point);
[[nodiscard]] std::string primitive_path_data(
    const std::string& name, const boost::property_tree::ptree& node);
void apply_transform(PathShape& path, const Affine2& transform);
[[nodiscard]] bool parse_svg_scalar(std::string text, float fallback, float& value);
[[nodiscard]] std::optional<std::string> collect_svg_gradients(
    const boost::property_tree::ptree& tree, SvgGradientMap& gradients,
    Color inherited_color = Color::black());

}  // namespace chronon3d::assets::svg_gradient_detail
