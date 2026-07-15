#pragma once

#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <optional>
#include <vector>

namespace chronon3d {

struct RectParams {
    Vec2 size{100, 100};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    std::optional<graphics::FillStyle> fill{};
    graphics::StrokeStyle stroke{};
};

struct RoundedRectParams {
    Vec2 size{100, 100};
    f32 radius{8.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    std::optional<graphics::FillStyle> fill{};
    graphics::StrokeStyle stroke{};
};

struct CircleParams {
    f32 radius{50.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    std::optional<graphics::FillStyle> fill{};
    graphics::StrokeStyle stroke{};
};

struct ShapeStrokeParams {
    f32 trim_start{0.0f};
    f32 trim_end{1.0f};
    bool enabled{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};
};

struct LineParams {
    Vec3 from{0, 0, 0};
    Vec3 to{100, 0, 0};
    f32 thickness{1.0f};
    Color color{1, 1, 1, 1};
    ShapeStrokeParams stroke{};
};

struct PathParams {
    std::vector<PathCommand> commands;
    PathStroke stroke{};
    Fill fill{};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    bool closed{false};
};

struct GridBackgroundParams {
    Vec2 size{1920.0f, 1080.0f};
    Vec2 offset{0.0f, 0.0f};
    Color bg_color{0.008f, 0.010f, 0.022f, 1.0f};
    Color grid_color{0.25f, 0.52f, 1.0f, 0.05f};
    f32 spacing{140.0f};
    f32 minor_thickness{1.25f};
    f32 major_thickness{2.75f};
    i32 major_every{4};
    bool centered{true};
};

} // namespace chronon3d
