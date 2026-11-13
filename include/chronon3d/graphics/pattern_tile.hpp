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

    /// objectBoundingBox patterns rasterize their content against a normalized
    /// unit box. The paint site supplies the concrete object bounds and expands
    /// this raster through `object_*`; `raster_scale` is the pixel density used
    /// while rasterizing the unit box.
    bool object_bounding_box{false};
    float object_x{0.0f};
    float object_y{0.0f};
    float object_width{1.0f};
    float object_height{1.0f};
    float raster_scale{1.0f};

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 &&
            static_cast<std::uint64_t>(width) * height * 4 == rgba.size();
    }
};

} // namespace chronon3d::graphics
