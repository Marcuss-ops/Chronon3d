#include <chronon3d/backends/text/text_renderer.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <spdlog/spdlog.h>
#include <type_traits>
#include "../software/utils/blend2d_bridge.hpp"
#include "../software/utils/blend2d_resources.hpp"

namespace chronon3d {

bool TextRenderer::draw_text(const TextShape& t, const Transform& tr, Framebuffer& fb,
                             const RenderState* state) {
    if (t.text.empty() || t.style.font_path.empty()) return true;

    BLFontFace face = blend2d_utils::Blend2DResources::instance().get_face(t.style.font_path);
    if (face.empty()) return false;

    const float effective_size = t.style.size * tr.scale.x;
    BLFont font;
    font.createFromFace(face, effective_size);

    // Calculate bounds using GlyphBuffer
    BLGlyphBuffer gb;
    gb.setUtf8Text(t.text.c_str(), t.text.size());
    font.shape(gb);
    
    BLTextMetrics metrics;
    font.getTextMetrics(gb, metrics);
    
    // Create a temporary image for the text block.
    // Padding to avoid clipping near edges
    const int tw = static_cast<int>(std::ceil(metrics.boundingBox.x1 - metrics.boundingBox.x0)) + 4;
    const int th = static_cast<int>(std::ceil(font.metrics().ascent + font.metrics().descent)) + 4;
    
    if (tw <= 0 || th <= 0) return true;

    BLImage img(tw, th, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.clearAll();
    
    BLRgba32 bl_color(
        static_cast<uint8_t>(std::clamp(t.style.color.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(t.style.color.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(t.style.color.b * 255.0f, 0.0f, 255.0f)),
        255 // We handle opacity in the bridge
    );
    
    ctx.setFillStyle(bl_color);
    ctx.fillUtf8Text(BLPoint(-metrics.boundingBox.x0 + 2, font.metrics().ascent + 2), font, t.text.c_str());
    ctx.end();

    float x_offset = 0.0f;
    const float full_width = metrics.advance.x;
    if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
    else if (t.style.align == TextAlign::Right) x_offset = -full_width;

    // Adjust for the bounding box offset
    x_offset += metrics.boundingBox.x0 - 2;

    float y_offset = -font.metrics().ascent - 2;
    if (t.style.align == TextAlign::Center) {
        // Visually center vertically as well when horizontal centering is used
        y_offset += (font.metrics().ascent - font.metrics().descent) * 0.5f;
    }

    blend2d_bridge::composite_bl_image(fb, img, 
        static_cast<int>(tr.position.x + x_offset), 
        static_cast<int>(tr.position.y + y_offset), 
        tr.opacity, BlendMode::Normal);

    return true;
}

} // namespace chronon3d
