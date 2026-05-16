#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include "../software/utils/blend2d_resources.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d {

std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& t,
    float effective_size,
    int padding
) {
    if (t.text.empty() || t.style.font_path.empty()) return std::nullopt;

    BLFontFace face = blend2d_utils::Blend2DResources::instance().get_face(t.style.font_path);
    if (face.empty()) return std::nullopt;

    BLFont font;
    font.createFromFace(face, effective_size);

    // Calculate bounds using GlyphBuffer
    BLGlyphBuffer gb;
    gb.setUtf8Text(t.text.c_str(), t.text.size());
    font.shape(gb);
    
    BLTextMetrics metrics;
    font.getTextMetrics(gb, metrics);
    
    const int tw = static_cast<int>(std::ceil(metrics.boundingBox.x1 - metrics.boundingBox.x0)) + padding;
    const int th = static_cast<int>(std::ceil(font.metrics().ascent + font.metrics().descent)) + padding;
    
    if (tw <= 0 || th <= 0) return std::nullopt;

    BLImage img(tw, th, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.clearAll();
    
    // We render in pure white (base for tinting or direct use)
    ctx.setFillStyle(BLRgba32(255, 255, 255, 255));
    ctx.fillUtf8Text(BLPoint(-metrics.boundingBox.x0 + (padding/2), font.metrics().ascent + (padding/2)), font, t.text.c_str());
    ctx.end();

    float x_offset = 0.0f;
    const float full_width = metrics.advance.x;
    if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
    else if (t.style.align == TextAlign::Right) x_offset = -full_width;

    // Adjust for the bounding box offset
    x_offset += metrics.boundingBox.x0 - (padding/2);

    float y_offset = -font.metrics().ascent - (padding/2);
    if (t.style.align == TextAlign::Center) {
        // Visually center vertically as well when horizontal centering is used
        y_offset += (font.metrics().ascent - font.metrics().descent) * 0.5f;
    }

    TextRasterization res;
    res.image = std::move(img);
    res.x_offset = x_offset;
    res.y_offset = y_offset;
    res.metrics = metrics;
    res.font = font;
    
    return res;
}

} // namespace chronon3d
