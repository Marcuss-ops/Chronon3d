#pragma once

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/video/video_encoder.hpp>
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

inline std::optional<video::HardwareEncoder> parse_hardware_encoder(const std::string& value) {
    std::string lower = lower_copy(value);

    if (lower == "none" || lower == "software" || lower == "off") {
        return video::HardwareEncoder::None;
    }
    if (lower == "auto") {
        return video::HardwareEncoder::Auto;
    }
    if (lower == "nvenc") {
        return video::HardwareEncoder::Nvenc;
    }
    if (lower == "qsv") {
        return video::HardwareEncoder::Qsv;
    }
    if (lower == "videotoolbox" || lower == "vt") {
        return video::HardwareEncoder::VideoToolbox;
    }
    if (lower == "amf") {
        return video::HardwareEncoder::Amf;
    }

    return std::nullopt;
}

} // namespace chronon3d::cli
