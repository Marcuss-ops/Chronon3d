#pragma once

#include <chronon3d/scene/model/shape/path.hpp>

#include <optional>
#include <array>
#include <cstdint>
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
    std::uint8_t components{1};
    float begin_seconds{0.0f};
    float duration_seconds{0.0f};
    bool discrete{false};
    /// `calcMode="spline"` cubic-bezier control points, in SVG keySplines
    /// order (x1, y1, x2, y2) for the single authored interval.
    std::array<float, 4> key_splines{};
    bool spline{false};
    /// `repeatCount` iterations, expanded into repeated keyframe cycles by the
    /// render-plan lowering. Bounded to keep the keyframe count finite.
    std::uint32_t repetitions{1};
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
};

struct SvgStyledElement {
    PathShape path;
    std::optional<Fill> fill;
    PathStroke stroke;
    float opacity{1.0f};
    /// Innermost isolated group (opacity < 1) of the element, if any.
    std::optional<SvgStyleGroupRef> group;
    std::vector<SvgAnimationSpec> animations;
    std::optional<SvgMorphAnimationSpec> morph_animation;
    std::vector<SvgGradientAnimationKeyframe> gradient_animation;
    bool gradient_animation_discrete{false};
};

} // namespace chronon3d::assets
