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
    c.a = j.value("a", 1.0f);
}

// Keyframe
template <typename T>
void to_json(nlohmann::json& j, const Keyframe<T>& k) {
    j = nlohmann::json{{"frame", k.frame}, {"value", k.value}};
}

template <typename T>
void from_json(const nlohmann::json& j, Keyframe<T>& k) {
    k.frame = j.at("frame").get<Frame>();
    j.at("value").get_to(k.value);
}

} // namespace chronon3d
