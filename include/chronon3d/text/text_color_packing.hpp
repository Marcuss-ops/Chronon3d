#pragma once

#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::text {

// Text colors arrive in Chronon's linear working domain. Authoring-boundary
// conversion (for example CSS hex parsing) happens exactly once before this
// function. This function is deliberately only quantization, so Vulkan,
// DirectYUV and fused text use one authority and cannot double-convert colors.
[[nodiscard]] inline std::uint32_t pack_text_color(const Color& color) noexcept {
    const auto to_byte = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(
            std::lround(value * 255.0f), 0L, 255L));
    };
    return to_byte(color.r) | (to_byte(color.g) << 8) |
           (to_byte(color.b) << 16) | (to_byte(color.a) << 24);
}

} // namespace chronon3d::text
