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

struct SvgStyledElement {
    PathShape path;
    std::optional<Fill> fill;
    PathStroke stroke;
    float opacity{1.0f};
    std::vector<SvgAnimationSpec> animations;
    std::optional<SvgMorphAnimationSpec> morph_animation;
    std::vector<SvgGradientAnimationKeyframe> gradient_animation;
    bool gradient_animation_discrete{false};
};

} // namespace chronon3d::assets
