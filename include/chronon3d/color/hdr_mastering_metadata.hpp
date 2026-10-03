#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace chronon3d::color {

struct ContentLightMetadata {
    std::uint32_t max_cll_nits{0};
    std::uint32_t max_fall_nits{0};
};

/// Explicit SMPTE ST 2086 mastering-display facts and optional CTA-861.3
/// content-light bounds. Coordinates are CIE 1931 xy in RGB order.
struct HdrMasteringMetadata {
    std::array<std::array<float, 2>, 3> display_primaries{};
    std::array<float, 2> white_point{};
    float min_luminance_nits{0.0f};
    float max_luminance_nits{0.0f};
    std::optional<ContentLightMetadata> content_light{};
};

} // namespace chronon3d::color
