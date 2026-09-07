#pragma once

#include <chronon3d/media/video/video_surface.hpp>

#include <cstdint>
#include <span>

namespace chronon3d::media::video {

struct VideoOverlay {
    VideoSurface surface{};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{1.0f}, dst_y1{1.0f};
    float opacity{1.0f};
};

struct OutputGeometry {
    std::uint32_t width{0};
    std::uint32_t height{0};
};

enum class ColorSpace : std::uint8_t { Bt601, Bt709 };

struct VideoCompositeRequest {
    VideoSurface source{};
    std::span<const VideoOverlay> overlays{};
    OutputGeometry geometry{};
    ColorSpace color_space{ColorSpace::Bt709};
};

struct CompositeResult {
    bool ok{false};
};

} // namespace chronon3d::media::video
