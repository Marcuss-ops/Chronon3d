#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

namespace chronon3d {

struct ContactShadowParams {
    Vec3 pos{0, 0, 0};
    Vec2 size{300, 80};
    f32 blur{30.0f};
    f32 opacity{0.45f};
    Color color{0, 0, 0, 1};
};

struct FakeBox3DParams {
    Vec3 pos{0, 0, 0};
    Vec2 size{200, 200};
    f32 depth{60};
    Color color{1, 1, 1, 1};
    f32 top_tint{0.15f};
    f32 side_tint{0.20f};
    bool contact_shadow{false};
    bool reflective{false};
    f32 floor_y{-210.0f};
};

struct GridPlaneParams {
    Vec3 pos{0, 0, 0};
    PlaneAxis axis{PlaneAxis::XZ};
    f32 extent{2000};
    f32 spacing{200};
    Color color{1, 1, 1, 0.25f};
    f32 fade_distance{1800.0f};
    f32 fade_min_alpha{0.0f};
};

struct DarkGridBgParams {
    Color bg_color{0.098f, 0.098f, 0.11f, 1.0f};
    Color grid_color{1.0f, 1.0f, 1.0f, 0.14f};
    f32 spacing{80.0f};
    f32 extent{4000.0f};
    bool centered{false};
};

} // namespace chronon3d
