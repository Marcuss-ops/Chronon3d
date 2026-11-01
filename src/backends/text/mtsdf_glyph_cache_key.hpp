#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::text {

struct MtsdfGlyphCacheKey {
    assets::ContentDigest font_digest{};
    std::uint32_t glyph_id{0};
    std::string variation_axes;
    MtsdfProfile profile{};

    friend bool operator==(const MtsdfGlyphCacheKey&,
                           const MtsdfGlyphCacheKey&) = default;
};

struct MtsdfGlyphCacheKeyHash {
    [[nodiscard]] std::size_t operator()(
        const MtsdfGlyphCacheKey& key) const noexcept {
        auto hash = core::hash::HashBuilder{}
            .add("chronon.mtsdf-glyph-cache.v2")
            .add_bytes(key.font_digest.bytes.data(), key.font_digest.bytes.size())
            .add(key.glyph_id)
            .add(std::string_view(key.variation_axes))
            .add(key.profile.em_px)
            .add(key.profile.distance_range_px)
            .add(key.profile.padding_px)
            .add(key.profile.generation_id);
        return static_cast<std::size_t>(hash.finish());
    }
};

} // namespace chronon3d::text
