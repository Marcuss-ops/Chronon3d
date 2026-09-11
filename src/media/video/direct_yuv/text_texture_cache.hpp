#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <string>
#include <vector>

namespace chronon3d::media::video {

struct RasterizedTextTexture {
    int width{0};
    int height{0};
    std::vector<float> gpu_rgba;
    assets::ContentDigest digest{};
};

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape& prepared_shape,
    const Color& fill_color,
    const Color& stroke_color,
    float stroke_width,
    bool has_background,
    const Color& bg_color,
    float bg_opacity,
    float bg_radius,
    float pad_x,
    float pad_y,
    float box_w,
    float box_h,
    const std::string& alignment);

} // namespace chronon3d::media::video
