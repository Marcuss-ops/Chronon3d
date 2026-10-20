#pragma once

#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <string_view>

namespace chronon3d::cli {

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
