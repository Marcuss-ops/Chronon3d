#include "software_text_processor_internal.hpp"
#include "../utils/blend2d_bridge.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::renderer {

void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, const TextRasterization& raster) {
    CHRONON_ZONE_C("text_glow", trace_category::kText);
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    const bool use_geo_transform = !state.projection.ready &&
                                   is_affine_transform(model) &&
                                   has_non_translation(model);

    const CacheKey key = hash_glow_params(node, node.shape.text.style.size);
    std::shared_ptr<BLImage> glow_cache;
    {
        std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
        auto cached = get_glow_cache().get(key);
        if (cached) {
            glow_cache = *cached;
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_glow_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (!glow_cache) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_glow_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        BLImage glow_img;
        glow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(glow_img);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.blitImage(BLPoint(0, 0), raster.image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(node.glow.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.glow.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.glow.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }

        auto cached_img = std::make_shared<BLImage>(glow_img);

        if (node.glow.radius > 0.0f) {
            // Blur requires pixel-level operations, so we use the FB path via renderer
            auto glow_fb = std::make_shared<Framebuffer>(glow_img.width(), glow_img.height());
            glow_fb->clear(Color::transparent());
            blend2d_bridge::composite_bl_image(*glow_fb, glow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*glow_fb, node.glow.radius);
            
            // For simplicity when blur is needed, just don't cache as BLImage
            cached_img = nullptr; // Signal no caching for blurry glows
        }

        if (cached_img) {
            std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
            size_t weight = cached_img->width() * cached_img->height() * 4;
            get_glow_cache().put(key, cached_img, weight);
            glow_cache = cached_img;
        }
    }

    const f32 glow_intensity_opacity = node.glow.intensity * node.glow.color.a;

    if (glow_cache) {
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster.x_offset));
            int y = static_cast<int>(std::lround(raster.y_offset));
            blend2d_bridge::composite_bl_image(fb, *glow_cache, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
        } else {
            Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
            blend2d_bridge::composite_bl_image_transformed(fb, *glow_cache, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
        }
    } else if (node.glow.radius > 0.0f) {
        // Fallback for blurry glow: render directly without caching
        BLImage glow_img;
        glow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(glow_img);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.blitImage(BLPoint(0, 0), raster.image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(node.glow.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.glow.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.glow.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }
        
        auto glow_fb = std::make_shared<Framebuffer>(glow_img.width(), glow_img.height());
        glow_fb->clear(Color::transparent());
        blend2d_bridge::composite_bl_image(*glow_fb, glow_img, 0, 0, 1.0f, BlendMode::Normal);
        renderer.apply_blur(*glow_fb, node.glow.radius);
        
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster.x_offset));
            int y = static_cast<int>(std::lround(raster.y_offset));
            blend2d_bridge::composite_framebuffer(fb, *glow_fb, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
        } else {
            Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
            blend2d_bridge::composite_framebuffer_transformed(fb, *glow_fb, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
        }
    }
}

} // namespace chronon3d::renderer
