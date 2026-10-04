#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/graphics/stroke/brush_profile.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/path_commands.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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
    std::optional<graphics::BrushProfile> brush;
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

/// Straight two-point stroke lowered into the canonical PathShape payload.
struct PointToPointPath {
    Vec2 start{0.0f, 0.0f};
    Vec2 end{0.0f, 0.0f};

    [[nodiscard]] PathShape lower(PathStroke stroke = {}) const {
        if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
            !std::isfinite(end.x) || !std::isfinite(end.y) ||
            glm::length(end - start) <= 1.0e-6f)
            throw std::invalid_argument("point-to-point path endpoints must be finite and distinct");
        if (!std::isfinite(stroke.width) || stroke.width < 0.0f ||
            !std::isfinite(stroke.trim_start) || !std::isfinite(stroke.trim_end) ||
            !std::isfinite(stroke.dash_offset) ||
            !std::isfinite(stroke.taper_start) || !std::isfinite(stroke.taper_end) ||
            stroke.trim_start < 0.0f || stroke.trim_start > 1.0f ||
            stroke.trim_end < 0.0f || stroke.trim_end > 1.0f ||
            stroke.trim_start >= stroke.trim_end ||
            stroke.taper_start < 0.0f || stroke.taper_start > 1.0f ||
            stroke.taper_end < 0.0f || stroke.taper_end > 1.0f ||
            std::any_of(stroke.dash_array.begin(), stroke.dash_array.end(), [](f32 dash) {
                return !std::isfinite(dash) || dash < 0.0f;
            }) || (stroke.brush && (!std::isfinite(stroke.width) || stroke.width < 0.0f)) ||
            (stroke.brush && !stroke.brush->valid()) ||
            (stroke.brush && stroke.width <= 0.0f && stroke.brush->size <= 0.0f) ||
            (stroke.brush && (stroke.taper_start != 1.0f || stroke.taper_end != 1.0f)) ||
            (stroke.brush && stroke.gradient.has_value()))

            throw std::invalid_argument("point-to-point stroke parameters are invalid");
        PathShape path;
        path.commands = {PathCommand::move_to(start), PathCommand::line_to(end)};
        path.stroke = std::move(stroke);
        path.fill.enabled = false;
        return path;
    }
};

} // namespace chronon3d
