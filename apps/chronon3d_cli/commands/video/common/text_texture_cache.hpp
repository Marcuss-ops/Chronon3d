#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli {

struct RasterizedTextTexture {
    int width{0};
    int height{0};
    std::vector<float> gpu_rgba;
    assets::ContentDigest digest{};
};

// Rasterize one already-resolved text primitive to a premultiplied linear RGBA
// texture. The font path must name a prepared asset; this function never
// searches system fonts or upstream repository testdata.
RasterizedTextTexture rasterize_text_texture(
    const std::string& text,
    const std::string& prepared_font_path,
    float font_size,
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

} // namespace chronon3d::cli
