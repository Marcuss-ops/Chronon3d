#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/vector/shape_style.hpp>

namespace chronon3d {

struct ArrowParams {
    Vec2 from{-50.0f, 0.0f};
    Vec2 to{50.0f, 0.0f};
    f32 thickness{10.0f};
    f32 head_width{30.0f};
    f32 head_length{30.0f};
    ShapeStyle style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct StarParams {
    Vec2 center{0.0f, 0.0f};
    int points{5};
    f32 inner_radius{20.0f};
    f32 outer_radius{50.0f};
    ShapeStyle style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct BadgeParams {
    Vec2 center{0.0f, 0.0f};
    f32 radius{50.0f};
    int scallops{16};
    f32 scallop_depth{5.0f};
    ShapeStyle style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SpeechBubbleParams {
    Vec2 center{0.0f, 0.0f};
    Vec2 size{120.0f, 80.0f};
    f32 corner_radius{15.0f};
    Vec2 pointer_tip{0.0f, -60.0f};
    f32 pointer_width{20.0f};
    f32 pointer_center{0.0f};
    ShapeStyle style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct CalloutParams {
    Vec2 rect_center{0.0f, 0.0f};
    Vec2 rect_size{120.0f, 60.0f};
    f32 corner_radius{5.0f};
    Vec2 target_point{100.0f, 100.0f};
    f32 pointer_width{15.0f};
    ShapeStyle style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ProgressBarParams {
    Vec2 size{300.0f, 20.0f};
    f32 progress{0.5f};
    f32 corner_radius{10.0f};
    ShapeStyle background_style{};
    ShapeStyle fill_style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct TimelineBarParams {
    Vec2 size{400.0f, 15.0f};
    f32 start{0.2f};
    f32 end{0.8f};
    f32 corner_radius{7.5f};
    ShapeStyle background_style{};
    ShapeStyle fill_style{};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

std::vector<PathCommand> make_rounded_rect_commands(Vec2 center, Vec2 size, f32 r);

PathParams make_arrow(const ArrowParams& p);
PathParams make_star(const StarParams& p);
PathParams make_badge(const BadgeParams& p);
PathParams make_speech_bubble(const SpeechBubbleParams& p);
PathParams make_callout(const CalloutParams& p);

} // namespace chronon3d
