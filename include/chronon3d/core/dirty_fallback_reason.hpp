#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <array>
#include <cstddef>
#include <string_view>

namespace chronon3d {

enum class DirtyFallbackReason : std::size_t {
    PredictedBoundsMissing = 0,
    CompositeMissingInputBounds = 1,
    TransformBoundsUnknown = 2,
    EffectBoundsUnknown = 3,
    Count = 4
};

[[nodiscard]] constexpr std::size_t dirty_fallback_reason_count() {
    return static_cast<std::size_t>(DirtyFallbackReason::Count);
}

[[nodiscard]] inline std::string to_string(DirtyFallbackReason reason) {
    return enum_utils::enum_name_lower_snake(reason);
}

[[nodiscard]] constexpr std::array<std::string_view, dirty_fallback_reason_count()> dirty_fallback_reason_names() {
    return {
        "predicted_bounds_missing",
        "composite_missing_input_bounds",
        "transform_bounds_unknown",
        "effect_bounds_unknown"
    };
}

} // namespace chronon3d
