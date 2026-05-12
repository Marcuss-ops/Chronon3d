#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/time.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/animation/keyframe.hpp>
#include <chronon3d/animation/animated_value.hpp>
#include <nlohmann/json.hpp>

namespace chronon3d {

// Vec3
inline void to_json(nlohmann::json& j, const Vec3& v) {
    j = nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}
inline void from_json(const nlohmann::json& j, Vec3& v) {
    j.at("x").get_to(v.x);
    j.at("y").get_to(v.y);
    j.at("z").get_to(v.z);
}

// Color
inline void to_json(nlohmann::json& j, const Color& c) {
    j = nlohmann::json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}
inline void from_json(const nlohmann::json& j, Color& c) {
    j.at("r").get_to(c.r);
    j.at("g").get_to(c.g);
    j.at("b").get_to(c.b);
    if (j.contains("a")) j.at("a").get_to(c.a); else c.a = 1.0f;
}

// Frame
inline void to_json(nlohmann::json& j, const Frame& f) {
    j = f.index;
}
inline void from_json(const nlohmann::json& j, Frame& f) {
    f.index = j.get<i64>();
}

// Keyframe
template <typename T>
void to_json(nlohmann::json& j, const Keyframe<T>& k) {
    j = nlohmann::json{{"frame", k.frame.index}, {"value", k.value}};
    // Add easing here later
}
template <typename T>
void from_json(const nlohmann::json& j, Keyframe<T>& k) {
    k.frame.index = j.at("frame").get<i64>();
    j.at("value").get_to(k.value);
}

// AnimatedValue
template <typename T>
void to_json(nlohmann::json& j, const AnimatedValue<T>& v) {
    j = nlohmann::json{{"default", v.evaluate(0)}, {"keyframes", v.keyframes()}};
}
// from_json for AnimatedValue is a bit more manual since we need to add_keyframe

} // namespace chronon3d
