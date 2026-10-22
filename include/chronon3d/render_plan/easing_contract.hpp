#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace chronon3d::render_plan::detail {

// RenderPlan keeps wire-facing names as strings for JSON compatibility. C++
// semantic checks use these canonical names rather than repeating literals.
namespace easing_name {
inline constexpr char kLinear[] = "linear";
inline constexpr char kInQuad[] = "in_quad";
inline constexpr char kOutQuad[] = "out_quad";
inline constexpr char kInOutQuad[] = "in_out_quad";
inline constexpr char kInCubic[] = "in_cubic";
inline constexpr char kOutCubic[] = "out_cubic";
inline constexpr char kInOutCubic[] = "in_out_cubic";
inline constexpr char kInExpo[] = "in_expo";
inline constexpr char kOutExpo[] = "out_expo";
inline constexpr char kInOutExpo[] = "in_out_expo";
inline constexpr char kInSine[] = "in_sine";
inline constexpr char kOutSine[] = "out_sine";
inline constexpr char kInOutSine[] = "in_out_sine";
inline constexpr char kInBack[] = "in_back";
inline constexpr char kOutBack[] = "out_back";
inline constexpr char kInOutBack[] = "in_out_back";
inline constexpr char kInElastic[] = "in_elastic";
inline constexpr char kOutElastic[] = "out_elastic";
inline constexpr char kInOutElastic[] = "in_out_elastic";
inline constexpr char kInBounce[] = "in_bounce";
inline constexpr char kOutBounce[] = "out_bounce";
inline constexpr char kInOutBounce[] = "in_out_bounce";
inline constexpr char kSmoothstep[] = "smoothstep";
inline constexpr char kHold[] = "hold";
inline constexpr char kBezier[] = "bezier";
inline constexpr char kSpring[] = "spring";
} // namespace easing_name

// Keyframe interpolation is a distinct JSON field, even where its wire names
// overlap the easing vocabulary. Keep its extra mode in a separate contract.
namespace interpolation_name {
inline constexpr std::string_view kLinear = easing_name::kLinear;
inline constexpr std::string_view kHold = easing_name::kHold;
inline constexpr std::string_view kBezier = easing_name::kBezier;
inline constexpr std::string_view kAutoBezier = "auto_bezier";
inline constexpr std::string_view kStep = "step";
} // namespace interpolation_name

inline constexpr std::array<std::string_view, 22> kStandardEasings{
    easing_name::kLinear, easing_name::kInQuad, easing_name::kOutQuad,
    easing_name::kInOutQuad, easing_name::kInCubic, easing_name::kOutCubic,
    easing_name::kInOutCubic, easing_name::kInExpo, easing_name::kOutExpo,
    easing_name::kInOutExpo, easing_name::kInSine, easing_name::kOutSine,
    easing_name::kInOutSine, easing_name::kInBack, easing_name::kOutBack,
    easing_name::kInOutBack, easing_name::kInElastic, easing_name::kOutElastic,
    easing_name::kInOutElastic, easing_name::kInBounce, easing_name::kOutBounce,
    easing_name::kInOutBounce};

[[nodiscard]] inline bool is_standard_easing(std::string_view easing) noexcept {
    return std::find(kStandardEasings.begin(), kStandardEasings.end(), easing) !=
           kStandardEasings.end();
}

[[nodiscard]] inline bool is_axis_easing(std::string_view easing) noexcept {
    return is_standard_easing(easing) || easing == easing_name::kSmoothstep ||
           easing == easing_name::kHold;
}

[[nodiscard]] inline bool is_stagger_easing(std::string_view easing) noexcept {
    return is_axis_easing(easing) || easing == easing_name::kBezier;
}

[[nodiscard]] inline bool is_counter_easing(std::string_view easing) noexcept {
    return is_standard_easing(easing) || easing == easing_name::kSmoothstep;
}

[[nodiscard]] inline bool is_animation_easing(std::string_view easing) noexcept {
    return is_axis_easing(easing) || easing == easing_name::kBezier ||
           easing == easing_name::kSpring;
}

} // namespace chronon3d::render_plan::detail
