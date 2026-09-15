#pragma once

#include <cstdint>
#include <vector>

namespace chronon3d {

/// Canonical CPU payload for one MTSDF glyph. `rgba8` stores one
/// bit-preserving float per packed RGBA8 texel, matching both the paged Vulkan
/// atlas upload and the DirectYUV packed texture lowering.
struct MtsdfGlyphBitmap {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::int32_t x_offset{0};
    std::int32_t y_offset{0};
    float advance_x{0.0f};
    std::vector<float> rgba8;
};

} // namespace chronon3d
