#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>

namespace chronon3d {

enum class MaskType {
    None,
    Rect,
    RoundedRect,
    Circle
};

struct Mask {
    MaskType type{MaskType::None};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    f32  radius{0.0f};
    bool inverted{false};

    [[nodiscard]] bool enabled() const { return type != MaskType::None; }
};

// Params used in LayerBuilder API
struct RectMaskParams {
    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    bool inverted{false};
};

struct RoundedRectMaskParams {
    Vec2 size{100.0f, 100.0f};
    f32  radius{8.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    bool inverted{false};
};

struct CircleMaskParams {
    f32  radius{50.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    bool inverted{false};
};

} // namespace chronon3d
