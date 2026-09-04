#pragma once

#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/core/string_utils.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::cli {

using chronon3d::lower_copy;

inline std::optional<animation::MotionAxis> parse_motion_axis(const std::string& axis) {
    std::string lower = lower_copy(axis);
    if (lower == "tilt") return animation::MotionAxis::Tilt;
    if (lower == "pan") return animation::MotionAxis::Pan;
    if (lower == "roll") return animation::MotionAxis::Roll;
    return std::nullopt;
}

inline graph::BackendPreference parse_backend_preference(std::string_view value) noexcept {
    if (value == "software") return graph::BackendPreference::Software;
    if (value == "vulkan") return graph::BackendPreference::GPU;
    return graph::BackendPreference::Auto;
}

inline MotionBlurMode parse_motion_blur_mode(int value) noexcept {
    switch (value) {
        case 1: return MotionBlurMode::TemporalAccumulation;
        case 2: return MotionBlurMode::VelocityApproximation;
        default: return MotionBlurMode::Off;
    }
}

inline TemporalSamplePattern parse_motion_blur_pattern(int value) noexcept {
    switch (value) {
        case 0: return TemporalSamplePattern::Uniform;
        case 2: return TemporalSamplePattern::Halton;
        default: return TemporalSamplePattern::Stratified;
    }
}

inline TemporalFilter parse_motion_blur_filter(int value) noexcept {
    switch (value) {
        case 1: return TemporalFilter::Triangle;
        case 2: return TemporalFilter::Gaussian;
        default: return TemporalFilter::Box;
    }
}

} // namespace chronon3d::cli
