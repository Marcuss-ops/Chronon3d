#include <chronon3d/backends/text/text_renderer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <spdlog/spdlog.h>
#include "../software/utils/blend2d_bridge.hpp"

namespace chronon3d {

bool TextRenderer::draw_text(const TextShape& t, const Transform& tr, Framebuffer& fb,
                             const RenderState* state) {
    if (t.text.empty() || t.style.font_path.empty()) return true;

    const float effective_size = t.style.size * tr.scale.x;
    auto raster = rasterize_text_to_bl_image(t, effective_size);
    if (!raster) return false;

    // 1. Draw Drop Shadows
    for (const auto& shadow : t.style.shadows) {
        if (!shadow.enabled || shadow.opacity <= 0.0f || shadow.color.a <= 0.0f) continue;
        
        BLImage shadow_img;
        shadow_img.create(raster->image.width(), raster->image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(shadow_img);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.blitImage(BLPoint(0, 0), raster->image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(shadow.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(shadow.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(shadow.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }

        blend2d_bridge::composite_bl_image(fb, shadow_img, 
            static_cast<int>(tr.position.x + raster->x_offset + shadow.offset.x), 
            static_cast<int>(tr.position.y + raster->y_offset + shadow.offset.y), 
            tr.opacity * shadow.opacity * shadow.color.a, BlendMode::Normal);
    }

    // 2. Draw Text (already colored and styled internally)
    blend2d_bridge::composite_bl_image(fb, raster->image, 
        static_cast<int>(tr.position.x + raster->x_offset), 
        static_cast<int>(tr.position.y + raster->y_offset), 
        tr.opacity, BlendMode::Normal);

    return true;
}

} // namespace chronon3d
