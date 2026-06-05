#include "software_text_effects.hpp"

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include "../utils/blend2d_bridge.hpp"

#include <blend2d.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <memory>
#include <mutex>

namespace chronon3d::renderer {

using CacheKey = u64;
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;

namespace {

BLRgba32 to_bl_rgba(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

bool is_affine_transform(const Mat4& m) {
    return
        std::abs(m[0][2]) < 1e-5f &&
        std::abs(m[1][2]) < 1e-5f &&
        std::abs(m[2][0]) < 1e-5f &&
        std::abs(m[2][1]) < 1e-5f &&
        std::abs(m[2][2] - 1.0f) < 1e-5f &&
        std::abs(m[2][3]) < 1e-5f &&
        std::abs(m[3][2]) < 1e-5f;
}

bool has_non_translation(const Mat4& m) {
    return
        std::abs(m[0][0] - 1.0f) > 1e-5f ||
        std::abs(m[0][1]) > 1e-5f ||
        std::abs(m[1][0]) > 1e-5f ||
        std::abs(m[1][1] - 1.0f) > 1e-5f;
}

size_t resolve_cache_max_mb(const char* env_name, size_t default_mb) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_mb * 1024ULL * 1024ULL;
    try {
        size_t mb = static_cast<size_t>(std::stoull(env));
        return mb > 0 ? mb * 1024ULL * 1024ULL : default_mb * 1024ULL * 1024ULL;
    } catch (...) {
        return default_mb * 1024ULL * 1024ULL;
    }
}

ShadowCache& get_shadow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

ShadowCache& get_glow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_GLOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

std::mutex g_text_glow_cache_mutex;
std::mutex g_text_shadow_cache_mutex;

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_text_style_full;

CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    return hash_text_style_full(text, effective_size, 0);
}

CacheKey hash_glow_params(const RenderNode& node, float effective_size) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(node.glow.radius));
    seed = hash_combine(seed, hash_value(node.glow.intensity));
    seed = hash_combine(seed, hash_value(node.glow.color.r));
    seed = hash_combine(seed, hash_value(node.glow.color.g));
    seed = hash_combine(seed, hash_value(node.glow.color.b));
    seed = hash_combine(seed, hash_value(node.glow.color.a));
    return seed;
}

CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(index));
    const auto& shadow = node.shape.text.style.shadows[index];
    seed = hash_combine(seed, hash_value(shadow.blur));
    seed = hash_combine(seed, hash_value(shadow.opacity));
    seed = hash_combine(seed, hash_value(shadow.color.r));
    seed = hash_combine(seed, hash_value(shadow.color.g));
    seed = hash_combine(seed, hash_value(shadow.color.b));
    seed = hash_combine(seed, hash_value(shadow.color.a));
    return seed;
}

} // namespace

void draw_text_glow(
    SoftwareRenderer& renderer,
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const TextRasterization& raster,
    float effective_size
) {
    CHRONON_ZONE_C("text_glow", trace_category::kText);
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    const bool use_geo_transform = !state.projection.ready &&
                                   is_affine_transform(model) &&
                                   has_non_translation(model);

    const CacheKey key = hash_glow_params(node, effective_size);
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
            Color glow_color = node.glow.color;
            glow_color.a = 1.0f;
            ctx.setFillStyle(to_bl_rgba(glow_color));
            ctx.fillAll();
        }

        auto cached_img = std::make_shared<BLImage>(glow_img);

        if (node.glow.radius > 0.0f) {
            auto glow_fb = renderer.framebuffer_pool()->acquire(glow_img.width(), glow_img.height(), true);
            chronon3d::blend2d_bridge::composite_bl_image(*glow_fb, glow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*glow_fb, node.glow.radius);

            const f32 glow_intensity_opacity = node.glow.intensity * node.glow.color.a;
            if (use_geo_transform) {
                int x = static_cast<int>(std::lround(raster.x_offset));
                int y = static_cast<int>(std::lround(raster.y_offset));
                chronon3d::blend2d_bridge::composite_framebuffer(fb, *glow_fb, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
            } else {
                Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
                chronon3d::blend2d_bridge::composite_framebuffer_transformed(fb, *glow_fb, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
            }
            return;
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
            chronon3d::blend2d_bridge::composite_bl_image(fb, *glow_cache, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
        } else {
            Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
            chronon3d::blend2d_bridge::composite_bl_image_transformed(fb, *glow_cache, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
        }
    }
}

void draw_text_shadow(
    SoftwareRenderer& renderer,
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const TextRasterization& raster,
    const TextShadow& shadow,
    size_t index,
    float effective_size
) {
    CHRONON_ZONE_C("text_shadow", trace_category::kText);
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    const bool use_geo_transform = !state.projection.ready &&
                                   is_affine_transform(model) &&
                                   has_non_translation(model);

    const CacheKey key = hash_shadow_params(node, effective_size, index);
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
            Color shadow_color_tint = shadow.color;
            shadow_color_tint.a = 1.0f;
            ctx.setFillStyle(to_bl_rgba(shadow_color_tint));
            ctx.fillAll();
        }

        auto cached_img = std::make_shared<BLImage>(shadow_img);

        if (shadow.blur > 0.0f) {
            auto shadow_fb = renderer.framebuffer_pool()->acquire(shadow_img.width(), shadow_img.height(), true);
            chronon3d::blend2d_bridge::composite_bl_image(*shadow_fb, shadow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*shadow_fb, shadow.blur);

            const f32 shadow_opacity = shadow.opacity * shadow.color.a;
            if (use_geo_transform) {
                int x = static_cast<int>(std::lround(raster.x_offset + shadow.offset.x));
                int y = static_cast<int>(std::lround(raster.y_offset + shadow.offset.y));
                chronon3d::blend2d_bridge::composite_framebuffer(fb, *shadow_fb, x, y, opacity * shadow_opacity, BlendMode::Normal);
            } else {
                Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
                chronon3d::blend2d_bridge::composite_framebuffer_transformed(fb, *shadow_fb, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
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
            chronon3d::blend2d_bridge::composite_bl_image(fb, *shadow_cache, x, y, opacity * shadow_opacity, BlendMode::Normal);
        } else {
            Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
            chronon3d::blend2d_bridge::composite_bl_image_transformed(fb, *shadow_cache, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
        }
    }
}

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    get_glow_cache().clear();
}

void clear_text_shadow_cache() {
    std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
    get_shadow_cache().clear();
}

} // namespace chronon3d::renderer
