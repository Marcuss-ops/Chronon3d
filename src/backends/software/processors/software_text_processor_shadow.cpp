#include "software_text_processor_internal.hpp"
#include "../utils/blend2d_bridge.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::renderer {

void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, const TextRasterization& raster, const TextShadow& shadow, size_t index) {
    CHRONON_ZONE_C("text_shadow", trace_category::kText);
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    const bool use_geo_transform = !state.projection.ready &&
                                   is_affine_transform(model) &&
                                   has_non_translation(model);

    const CacheKey key = hash_shadow_params(node, node.shape.text.style.size, index);
    std::shared_ptr<BLImage> shadow_cache;
    {
        std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
        auto cached = get_shadow_cache().get(key);
        if (cached) {
            shadow_cache = *cached;
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_shadow_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (!shadow_cache) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_shadow_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        BLImage shadow_img;
        shadow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(shadow_img);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.blitImage(BLPoint(0, 0), raster.image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(shadow.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(shadow.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(shadow.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }

        auto cached_img = std::make_shared<BLImage>(shadow_img);

        if (shadow.blur > 0.0f) {
            auto shadow_fb = renderer.framebuffer_pool()->acquire(shadow_img.width(), shadow_img.height(), true);
            blend2d_bridge::composite_bl_image(*shadow_fb, shadow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*shadow_fb, shadow.blur);
            
            const f32 shadow_opacity = shadow.opacity * shadow.color.a;
            if (use_geo_transform) {
                int x = static_cast<int>(std::lround(raster.x_offset + shadow.offset.x));
                int y = static_cast<int>(std::lround(raster.y_offset + shadow.offset.y));
                blend2d_bridge::composite_framebuffer(fb, *shadow_fb, x, y, opacity * shadow_opacity, BlendMode::Normal);
            } else {
                Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
                blend2d_bridge::composite_framebuffer_transformed(fb, *shadow_fb, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
            }
            return;
        }

        if (cached_img) {
            std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
            size_t weight = cached_img->width() * cached_img->height() * 4;
            get_shadow_cache().put(key, cached_img, weight);
            shadow_cache = cached_img;
        }
    }

    const f32 shadow_opacity = shadow.opacity * shadow.color.a;

    if (shadow_cache) {
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster.x_offset + shadow.offset.x));
            int y = static_cast<int>(std::lround(raster.y_offset + shadow.offset.y));
            blend2d_bridge::composite_bl_image(fb, *shadow_cache, x, y, opacity * shadow_opacity, BlendMode::Normal);
        } else {
            Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
            blend2d_bridge::composite_bl_image_transformed(fb, *shadow_cache, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
        }
    }
}

} // namespace chronon3d::renderer
