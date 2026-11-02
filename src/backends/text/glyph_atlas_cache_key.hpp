#pragma once

#include <chronon3d/core/hash/hash_builder.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::detail {

// Path-based entries retain the complete font identity. The raw font-id
// overload remains a separate compatibility namespace because a digest alone
// cannot disambiguate two paths that happen to collide.
struct GlyphAtlasKey {
    bool path_identity{false};
    std::string font_path;
    std::uint64_t font_id{0};
    std::uint32_t glyph_id{0};
    std::uint32_t font_size{0};

    bool operator==(const GlyphAtlasKey& other) const noexcept {
        return path_identity == other.path_identity &&
            (path_identity ? font_path == other.font_path : font_id == other.font_id) &&
            glyph_id == other.glyph_id && font_size == other.font_size;
    }
};

struct GlyphAtlasKeyHash {
    [[nodiscard]] std::size_t operator()(const GlyphAtlasKey& key) const noexcept {
        auto hash = chronon3d::core::hash::HashBuilder{}
            .add("chronon.backend-glyph-atlas-key.v2")
            .add(key.path_identity);
        if (key.path_identity) hash.add(std::string_view(key.font_path));
        else hash.add(key.font_id);
        hash.add(key.glyph_id).add(key.font_size);
        return static_cast<std::size_t>(hash.finish());
    }
};

} // namespace chronon3d::detail
