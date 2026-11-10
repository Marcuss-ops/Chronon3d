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
    // Fractional plane origin in em pixels. The field texel centres sit at
    // (x_offset + x + 0.5, y_offset + y + 0.5); rounding this to an integer
    // before runtime scaling shifts every glyph by up to one em pixel.
    float x_offset{0.0f};
    float y_offset{0.0f};
    float advance_x{0.0f};
    std::vector<float> rgba8;
};

} // namespace chronon3d
