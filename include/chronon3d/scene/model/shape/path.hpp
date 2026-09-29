#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/path_commands.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <vector>

namespace chronon3d {

enum class PathFillRule : std::uint8_t {
    EvenOdd,
    NonZero,
};

enum class LineCap {
    Butt,
    Round,
    Square
};

enum class LineJoin {
    Miter,
    Round,
    Bevel
};

struct PathStroke {
    bool enabled{true};
    Color color{1, 1, 1, 1};
    f32 width{1.0f};
    LineCap cap{LineCap::Butt};
    LineJoin join{LineJoin::Miter};
    std::vector<f32> dash_array{};
    f32 dash_offset{0.0f};
    f32 trim_start{0.0f};
    f32 trim_end{1.0f};
    f32 taper_start{1.0f};
    f32 taper_end{1.0f};

    /// Optional gradient fill.  When present, the stroke colours come
    /// from the gradient instead of the solid `color` field.
    std::optional<GradientFill> gradient;
};

struct PathShape {
    std::vector<PathCommand> commands;
    PathStroke stroke{};
    Fill fill{};
    // Preserve the renderer's historical parity rule unless a caller opts in.
    PathFillRule fill_rule{PathFillRule::EvenOdd};
    bool closed{false};
    std::vector<PathOperatorSpec> operators{};
    f32 opacity{1.0f};
};

} // namespace chronon3d
