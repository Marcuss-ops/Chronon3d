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

    // Apply text color (rasterize_text_to_bl_image produces white text)
    BLContext ctx(raster->image);
    ctx.setCompOp(BL_COMP_OP_SRC_IN);
    ctx.setFillStyle(BLRgba32(
        static_cast<uint8_t>(std::clamp(t.style.color.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(t.style.color.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(t.style.color.b * 255.0f, 0.0f, 255.0f)),
        255
    ));
    ctx.fillAll();
    ctx.end();

    blend2d_bridge::composite_bl_image(fb, raster->image, 
        static_cast<int>(tr.position.x + raster->x_offset), 
        static_cast<int>(tr.position.y + raster->y_offset), 
        tr.opacity, BlendMode::Normal);

    return true;
}

} // namespace chronon3d
