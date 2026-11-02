#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::text {

struct FontIdentityStampKey {
    std::string resolved_path;
    std::uint64_t byte_size{0};
    std::int64_t write_time_ticks{0};
    bool byte_size_known{false};
    bool write_time_known{false};

    friend bool operator==(const FontIdentityStampKey&,
                           const FontIdentityStampKey&) = default;
};

struct FontIdentityStampKeyHash {
    [[nodiscard]] std::size_t operator()(
        const FontIdentityStampKey& key) const noexcept {
        return static_cast<std::size_t>(core::hash::HashBuilder{}
            .add("chronon.font-identity-stamp.v2")
            .add(std::string_view(key.resolved_path))
            .add(key.byte_size)
            .add(key.write_time_ticks)
            .add(key.byte_size_known)
            .add(key.write_time_known)
            .finish());
    }
};

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
