#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace chronon3d::render_plan::detail {

inline constexpr std::array<std::string_view, 22> kStandardEasings{
    "linear", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic",
    "in_out_cubic", "in_expo", "out_expo", "in_out_expo", "in_sine", "out_sine",
    "in_out_sine", "in_back", "out_back", "in_out_back", "in_elastic", "out_elastic",
    "in_out_elastic", "in_bounce", "out_bounce", "in_out_bounce"};

[[nodiscard]] inline bool is_standard_easing(std::string_view easing) noexcept {
    return std::find(kStandardEasings.begin(), kStandardEasings.end(), easing) !=
           kStandardEasings.end();
}

[[nodiscard]] inline bool is_axis_easing(std::string_view easing) noexcept {
    return is_standard_easing(easing) || easing == "smoothstep" || easing == "hold";
}

[[nodiscard]] inline bool is_stagger_easing(std::string_view easing) noexcept {
    return is_axis_easing(easing) || easing == "bezier";
}

[[nodiscard]] inline bool is_counter_easing(std::string_view easing) noexcept {
    return is_standard_easing(easing) || easing == "smoothstep";
}

[[nodiscard]] inline bool is_animation_easing(std::string_view easing) noexcept {
    return is_axis_easing(easing) || easing == "bezier" || easing == "spring";
}

} // namespace chronon3d::render_plan::detail
