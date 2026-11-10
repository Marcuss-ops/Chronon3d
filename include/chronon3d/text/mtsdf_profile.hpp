#pragma once

#include <cstdint>

namespace chronon3d::text {

/// Canonical distance-field generation contract shared by every lowering.
/// Layout size is applied at sampling time; these values describe the
/// immutable glyph payload and therefore belong to text preparation, not to a
/// backend-specific compositor.
struct MtsdfProfile final {
    std::uint16_t em_px{64};
    std::uint16_t distance_range_px{16};
    std::uint16_t padding_px{24};
    std::uint16_t generation_id{1};

    friend constexpr bool operator==(const MtsdfProfile&, const MtsdfProfile&) =
        default;
};

inline constexpr MtsdfProfile kCanonicalMtsdfProfile{};

/// High-quality generation profile. The higher em grid and expanded distance
/// extent preserve authored shadow reach instead of clipping it at the field
/// boundary. generation_id distinguishes this atlas payload and cache key
/// from earlier profile contracts.
inline constexpr MtsdfProfile kHighQualityMtsdfProfile{
    .em_px = 128,
    .distance_range_px = 40,
    .padding_px = 60,
    .generation_id = 4,
};

} // namespace chronon3d::text
