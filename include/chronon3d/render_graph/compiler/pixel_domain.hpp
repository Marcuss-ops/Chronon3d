// SPDX-License-Identifier: MIT
#pragma once

#include <chronon3d/render_graph/core/node_identity.hpp>
#include <cstdint>
#include <string_view>
#include <string>

namespace chronon3d::graph {

/// Pixel domain for operations and surfaces.
/// First-principle rule: Stay native as long as mathematically/hardware possible.
/// Convert only when an operation explicitly requires another domain.
enum class PixelDomain : std::uint8_t {
    NV12 = 0,    // 8-bit YUV 4:2:0 bi-planar (NVDEC/NVENC direct zero-copy native)
    P010,        // 10-bit YUV 4:2:0 bi-planar (HDR/10-bit video)
    RGBA8,       // 8-bit per channel linear/sRGB (standard SDR raster/text/UI)
    RGBA16F,     // 16-bit float (HDR, linear compositing, wide-gamut grading, blurs)
    RGBA32F,     // 32-bit float (high precision reference and simulation)
    Custom,      // Backend/hardware-specific opaque format
};

[[nodiscard]] constexpr std::string_view pixel_domain_name(PixelDomain domain) noexcept {
    switch (domain) {
        case PixelDomain::NV12:    return "NV12";
        case PixelDomain::P010:    return "P010";
        case PixelDomain::RGBA8:   return "RGBA8";
        case PixelDomain::RGBA16F: return "RGBA16F";
        case PixelDomain::RGBA32F: return "RGBA32F";
        case PixelDomain::Custom:  return "Custom";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool pixel_domain_is_yuv(PixelDomain domain) noexcept {
    return domain == PixelDomain::NV12 || domain == PixelDomain::P010;
}

[[nodiscard]] constexpr bool pixel_domain_is_rgb(PixelDomain domain) noexcept {
    return domain == PixelDomain::RGBA8 || domain == PixelDomain::RGBA16F || domain == PixelDomain::RGBA32F;
}

[[nodiscard]] constexpr std::size_t pixel_domain_bytes_per_pixel_approx(PixelDomain domain) noexcept {
    switch (domain) {
        case PixelDomain::NV12:    return 1; // 1.5 bytes per pixel (12 bpp)
        case PixelDomain::P010:    return 2; // 3 bytes per pixel (24 bpp)
        case PixelDomain::RGBA8:   return 4;
        case PixelDomain::RGBA16F: return 8;
        case PixelDomain::RGBA32F: return 16;
        case PixelDomain::Custom:  return 4;
    }
    return 4;
}

/// Explicit color domain barrier. Every domain conversion in the compiled plan
/// must correspond to a ColorDomainBarrier with a mathematically or hardware-justified reason.
struct ColorDomainBarrier {
    PixelDomain from_domain{PixelDomain::NV12};
    PixelDomain to_domain{PixelDomain::RGBA8};
    GraphNodeId producer{k_invalid_node};
    GraphNodeId consumer{k_invalid_node};
    std::string reason;
};

} // namespace chronon3d::graph
