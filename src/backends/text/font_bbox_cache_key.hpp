#pragma once

#include <chronon3d/core/hash/hash_builder.hpp>
#include <cstdint>
#include <string>

namespace chronon3d {

using FontFaceId = std::uint64_t;

struct GlyphBBoxCacheKey {
    FontFaceId face_id{0};
    std::uint32_t glyph_id{0};
    std::uint32_t pixel_size{0};
    std::string variation_axes;

    friend bool operator==(const GlyphBBoxCacheKey& a,
                           const GlyphBBoxCacheKey& b) noexcept {
        return a.face_id == b.face_id && a.glyph_id == b.glyph_id &&
            a.pixel_size == b.pixel_size && a.variation_axes == b.variation_axes;
    }
};

struct GlyphBBoxCacheKeyHash {
    [[nodiscard]] std::size_t operator()(const GlyphBBoxCacheKey& key) const noexcept {
        return static_cast<std::size_t>(core::hash::HashBuilder{}
            .add("chronon.font-glyph-bbox.v2")
            .add(key.face_id)
            .add(key.glyph_id)
            .add(key.pixel_size)
            .add(std::string_view(key.variation_axes))
            .finish());
    }
};

} // namespace chronon3d
