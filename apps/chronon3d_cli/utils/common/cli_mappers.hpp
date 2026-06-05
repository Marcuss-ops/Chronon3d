#pragma once

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/core/string_utils.hpp>
#include <optional>

namespace chronon3d::cli {

using chronon3d::lower_copy;

inline std::optional<animation::MotionAxis> parse_motion_axis(const std::string& axis) {
    std::string lower = lower_copy(axis);
    if (lower == "tilt") return animation::MotionAxis::Tilt;
    if (lower == "pan") return animation::MotionAxis::Pan;
    if (lower == "roll") return animation::MotionAxis::Roll;
    return std::nullopt;
}

} // namespace chronon3d::cli
