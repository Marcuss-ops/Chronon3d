#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace chronon3d::graphics {

/// Immutable CPU image used as a repeating paint source by software text.
/// Pixels are tightly packed, unpremultiplied RGBA8 in row-major order.
struct PatternTile {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<std::uint8_t> rgba;
    /// SVG affine matrix [a,b,c,d,e,f], applied to pattern user coordinates.
    std::array<float, 6> transform{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 &&
            static_cast<std::uint64_t>(width) * height * 4 == rgba.size();
    }
};

} // namespace chronon3d::graphics
