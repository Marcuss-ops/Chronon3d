#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace chronon3d {

#ifdef CHRONON3D_ENABLE_TEXT

/// Canonical CPU preparation result for one glyph. `rgba8` contains one
/// bit-preserving float cell per RGBA8 texel, matching the Vulkan upload ABI.
/// RGB are edge-coloured signed distances; alpha is the true signed distance.
struct MtsdfGlyphBitmap {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::int32_t x_offset{0};
    std::int32_t y_offset{0};
    float advance_x{0.0f};
    std::vector<float> rgba8;
};

/// Generate one canonical MTSDF glyph from a FreeType outline. The face must
/// already be loaded at `em_px`; callers normally obtain a cached face at the
/// atlas profile size (64 px), so the resulting key is independent of layout
/// font size.
[[nodiscard]] std::optional<MtsdfGlyphBitmap> generate_mtsdf_glyph(
    FT_Face face,
    std::uint32_t glyph_id,
    std::uint16_t em_px = 64,
    std::uint16_t distance_range_px = 8,
    std::uint16_t padding_px = 10);

#endif

} // namespace chronon3d
