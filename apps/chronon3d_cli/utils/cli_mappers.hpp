#pragma once

#include <chronon3d/animations/camera_motion.hpp>
#include <string>
#include <algorithm>
#include <optional>

namespace chronon3d::cli {

inline std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

inline std::optional<animation::MotionAxis> parse_motion_axis(const std::string& axis) {
    std::string lower = lower_copy(axis);
    if (lower == "tilt") return animation::MotionAxis::Tilt;
    if (lower == "pan") return animation::MotionAxis::Pan;
    if (lower == "roll") return animation::MotionAxis::Roll;
    return std::nullopt;
}

} // namespace chronon3d::cli
