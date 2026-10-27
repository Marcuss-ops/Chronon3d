#pragma once

#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/graphics/pattern_tile.hpp>

#include <optional>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::assets {

enum class SvgAnimationKind {
    Translate, Scale, Rotate, Opacity, FillColor, StrokeColor,
    StrokeWidth, FillOpacity, StrokeOpacity
};

struct SvgAnimationSpec {
    SvgAnimationKind kind{SvgAnimationKind::Opacity};
    std::array<float, 4> from{};
    std::array<float, 4> to{};
    // animateTransform rotate may carry one fixed center (cx, cy).
    std::array<float, 2> pivot{0.0f, 0.0f};
    bool has_pivot{false};
    std::uint8_t components{1};
    float begin_seconds{0.0f};
    float duration_seconds{0.0f};
    bool discrete{false};
};

struct SvgMorphAnimationSpec {
    std::vector<PathShape> paths; // first is the active base; remaining entries are morph targets
    std::vector<float> key_times;
    float duration_seconds{0.0f};
    bool discrete{false};
};

struct SvgGradientAnimationKeyframe {
    float seconds{0.0f};
    graphics::GradientDefinition gradient;
};

/// Innermost enclosing group that isolates its children: they must paint at
/// full alpha onto one shared surface and the surface composites once with
/// `group_opacity` (ADR-035). `group_id` 0 means "no isolated group".
struct SvgStyleGroupRef {
    std::uint32_t id{0};
    float opacity{1.0f};
    std::optional<SvgAnimationSpec> opacity_animation;
};

struct SvgStyledElement {
    PathShape path;
    std::optional<Fill> fill;
    PathStroke stroke;
    float opacity{1.0f};
    /// Innermost isolated group (opacity < 1) of the element, if any.
    std::optional<SvgStyleGroupRef> group;
    /// Full isolated-group ancestry, outermost first. `group` is retained as
    /// a compatibility view of the innermost entry for existing callers.
    std::vector<SvgStyleGroupRef> groups;
    std::vector<SvgAnimationSpec> animations;
    std::optional<SvgMorphAnimationSpec> morph_animation;
    std::vector<SvgGradientAnimationKeyframe> gradient_animation;
    bool gradient_animation_discrete{false};
    /// Original SVG element id. Empty when the source element has no id.
    std::string source_id;
    /// Original ids of enclosing SVG groups and <use> instances, outermost first.
    std::vector<std::string> group_ids;
    /// Clip/mask geometries scoped to this SVG element, outermost first.
    std::vector<PathShape> vector_masks;
};

/// One positioned text run imported from an SVG <text>/<tspan> tree. Runs are
/// lowered to ordinary Chronon text layers, so shaping and rasterization stay
/// on the canonical text path. Coordinates remain in SVG user space until the
/// RenderPlan compiler applies the root/group transforms.
struct SvgTextRun {
    std::string text;
    std::string text_element_id;
    std::string source_id;
    std::vector<std::string> group_ids;
    std::vector<std::string> coordinate_scope_ids;
    std::string text_anchor_scope_id;
    std::vector<float> x_coordinates;
    std::vector<float> y_coordinates;
    std::vector<float> dx_coordinates;
    std::vector<float> dy_coordinates;
    std::vector<float> rotate_degrees;
    float letter_spacing{0.0f};
    float word_spacing{0.0f};
    std::optional<float> text_length;
    std::string length_adjust{"spacing"};
    /// Full 2D points/deltas for coordinate lists under rotated or skewed
    /// SVG coordinate transforms (the scalar arrays are axis-aligned only).
    std::vector<std::array<float, 2>> transformed_position_coordinates;
    std::vector<std::array<float, 2>> transformed_delta_coordinates;
    bool positioned_text_cluster{false};
    bool shaping_mergeable{false};
    std::optional<float> x;
    std::optional<float> y;
    bool x_explicit{false};
    bool y_explicit{false};
    float dx{0.0f};
    float dy{0.0f};
    std::string font_family;
    float font_size{16.0f};
    int font_weight{400};
    std::string font_style{"normal"};
    std::string direction{"auto"};
    std::string text_anchor{"start"};
    std::string dominant_baseline{"auto"};
    float baseline_shift{0.0f};
    std::optional<std::string> text_path_id;
    std::optional<PathShape> text_path_geometry;
    float text_path_start_offset{0.0f};
    bool text_path_start_offset_percent{false};
    std::string fill{"#000000"};
    std::optional<graphics::GradientDefinition> fill_gradient;
    std::shared_ptr<const graphics::PatternTile> fill_pattern;
    float fill_opacity{1.0f};
    std::optional<std::string> stroke;
    std::optional<graphics::GradientDefinition> stroke_gradient;
    std::shared_ptr<const graphics::PatternTile> stroke_pattern;
    float stroke_width{1.0f};
    LineCap stroke_cap{LineCap::Butt};
    LineJoin stroke_join{LineJoin::Miter};
    float stroke_miter_limit{4.0f};
    std::vector<float> stroke_dash_array;
    float stroke_dash_offset{0.0f};
    float stroke_opacity{1.0f};
    float opacity{1.0f};
};

} // namespace chronon3d::assets
