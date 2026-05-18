#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/scene/shape.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::presets::motion {

enum class MotionObjectType {
    Text,
    Image,
    Rect,
    RoundedRect,
    Circle,
    Line,
    Group,
};

enum class MotionPreset {
    None,
    FadeIn,
    PopIn,
    PopGlow,
    SlideUp,
    SlideLeft,
    ZoomBlur,
    PushIn3D,
    ParallaxDrift,
    Orbit2_5D,
    ShakeImpact,
};

struct MotionTime {
    Frame start{0};
    Frame end{60};

    [[nodiscard]] bool contains(Frame f) const {
        return f >= start && f <= end;
    }

    [[nodiscard]] f32 normalized(Frame f) const {
        const f32 len = static_cast<f32>(end - start);
        if (len <= 0.0f) return 1.0f;
        return std::clamp(static_cast<f32>(f - start) / len, 0.0f, 1.0f);
    }
};

struct Motion3D {
    bool enabled{false};
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    f32 z{0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    f32 parallax{1.0f};
    bool face_camera{false};
};

struct TextStyleMotion {
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    std::string font_family{"Inter"};
    int font_weight{800};
    std::string font_style{"normal"};
    f32 font_size{72.0f};
    f32 tracking{0.0f};
    TextAlign align{TextAlign::Center};
};

struct MotionObject {
    std::string id;
    MotionObjectType type{MotionObjectType::Rect};
    MotionPreset preset_value{MotionPreset::None};

    MotionTime time_value{};
    Motion3D motion3d{};

    Vec3 position_value{0.0f, 0.0f, 0.0f};
    Vec2 size_value{100.0f, 100.0f};
    Vec3 scale_value{1.0f, 1.0f, 1.0f};
    Vec3 rotation_value{0.0f, 0.0f, 0.0f};

    Color color_value{1.0f, 1.0f, 1.0f, 1.0f};
    f32 opacity_value{1.0f};
    f32 radius_value{16.0f};

    std::string text_value;
    std::string image_path_value;
    TextStyleMotion text_style{};
    bool glow_enabled{false};

    Vec3 line_from_value{0.0f, 0.0f, 0.0f};
    Vec3 line_to_value{100.0f, 0.0f, 0.0f};
    f32 line_thickness_value{1.0f};

    std::vector<MotionObject> children;

    static MotionObject text(std::string id, std::string value) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::Text;
        o.text_value = std::move(value);
        o.size_value = {900.0f, 160.0f};
        return o;
    }

    static MotionObject image(std::string id, std::string path) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::Image;
        o.image_path_value = std::move(path);
        return o;
    }

    static MotionObject rect(std::string id) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::Rect;
        return o;
    }

    static MotionObject rounded_rect(std::string id) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::RoundedRect;
        return o;
    }

    static MotionObject circle(std::string id) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::Circle;
        return o;
    }

    static MotionObject line(std::string id) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::Line;
        return o;
    }

    static MotionObject group(std::string id, std::vector<MotionObject> items) {
        MotionObject o;
        o.id = std::move(id);
        o.type = MotionObjectType::Group;
        o.children = std::move(items);
        return o;
    }

    MotionObject& at(Vec3 p) {
        position_value = p;
        return *this;
    }

    MotionObject& size(Vec2 s) {
        size_value = s;
        return *this;
    }

    MotionObject& scale(Vec3 s) {
        scale_value = s;
        return *this;
    }

    MotionObject& rotate(Vec3 euler_deg) {
        rotation_value = euler_deg;
        return *this;
    }

    MotionObject& color(Color c) {
        color_value = c;
        return *this;
    }

    MotionObject& opacity(f32 value) {
        opacity_value = value;
        return *this;
    }

    MotionObject& radius(f32 value) {
        radius_value = value;
        return *this;
    }

    MotionObject& preset(MotionPreset value) {
        preset_value = value;
        return *this;
    }

    MotionObject& time(Frame start, Frame end) {
        time_value.start = start;
        time_value.end = end;
        return *this;
    }

    MotionObject& glow(bool enabled = true) {
        glow_enabled = enabled;
        return *this;
    }

    MotionObject& font_path(std::string path) {
        text_style.font_path = std::move(path);
        return *this;
    }

    MotionObject& font_family(std::string family) {
        text_style.font_family = std::move(family);
        return *this;
    }

    MotionObject& font_weight(int weight) {
        text_style.font_weight = weight;
        return *this;
    }

    MotionObject& font_size(f32 value) {
        text_style.font_size = value;
        return *this;
    }

    MotionObject& tracking(f32 value) {
        text_style.tracking = value;
        return *this;
    }

    MotionObject& align(TextAlign value) {
        text_style.align = value;
        return *this;
    }

    MotionObject& enable_3d(f32 z = 0.0f) {
        motion3d.enabled = true;
        motion3d.z = z;
        return *this;
    }

    MotionObject& motion_position(Vec3 p) {
        motion3d.enabled = true;
        motion3d.position = p;
        return *this;
    }

    MotionObject& motion_scale(Vec3 s) {
        motion3d.enabled = true;
        motion3d.scale = s;
        return *this;
    }

    MotionObject& rotate_3d(Vec3 euler_deg) {
        motion3d.enabled = true;
        motion3d.rotation = euler_deg;
        return *this;
    }

    MotionObject& parallax(f32 value) {
        motion3d.parallax = value;
        return *this;
    }

    MotionObject& face_camera(bool enabled = true) {
        motion3d.face_camera = enabled;
        return *this;
    }

    MotionObject& from(Vec3 p) {
        line_from_value = p;
        return *this;
    }

    MotionObject& to(Vec3 p) {
        line_to_value = p;
        return *this;
    }

    MotionObject& thickness(f32 value) {
        line_thickness_value = value;
        return *this;
    }

    MotionObject& add_child(MotionObject child) {
        children.push_back(std::move(child));
        return *this;
    }
};

} // namespace chronon3d::presets::motion
