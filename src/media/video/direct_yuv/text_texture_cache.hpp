#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <string>
#include <vector>

namespace chronon3d::media::video {

/// Prepared MTSDF atlas payload for the DirectYUV lowering. `gpu_rgba` keeps
/// one bit-preserving float cell per RGBA8 texel, exactly like the Vulkan
/// paged atlas transport. The CUDA compositor decodes the field at sample
/// time; no Blend2D or CPU RGBA raster is involved.
struct RasterizedTextTexture {
    int width{0};
    int height{0};
    float display_width{0.0f};
    float display_height{0.0f};
    float distance_range_px{8.0f};
    bool mtsdf{false};
    std::vector<float> gpu_rgba;
    assets::ContentDigest digest{};
};

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape& prepared_shape);

} // namespace chronon3d::media::video
