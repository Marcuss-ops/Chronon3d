#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <spdlog/spdlog.h>
#include "../utils/render_effects_processor.hpp"
#include "../utils/blend2d_bridge.hpp"
#include "../utils/blend2d_resources.hpp"
#include <chronon3d/core/counters.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>

#include <blend2d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>
#include <cstdlib>

namespace chronon3d::renderer {

namespace {

using CacheKey = u64;
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_string;
using chronon3d::graph::hash_bytes;

CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    CacheKey seed = 0;
    seed = hash_combine(seed, hash_string(text.text));
    seed = hash_combine(seed, hash_string(text.style.font_path));
    seed = hash_combine(seed, hash_string(text.style.font_family));
    seed = hash_combine(seed, hash_value(text.style.font_weight));
    seed = hash_combine(seed, hash_string(text.style.font_style));
    seed = hash_combine(seed, hash_value(effective_size));
    seed = hash_combine(seed, hash_value(text.style.color.r));
    seed = hash_combine(seed, hash_value(text.style.color.g));
    seed = hash_combine(seed, hash_value(text.style.color.b));
    seed = hash_combine(seed, hash_value(text.style.color.a));
    seed = hash_combine(seed, hash_value(static_cast<int>(text.style.align)));
    seed = hash_combine(seed, hash_value(text.style.line_height));
    seed = hash_combine(seed, hash_value(text.style.tracking));
    seed = hash_combine(seed, hash_value(text.style.max_lines));
    seed = hash_combine(seed, hash_value(text.style.auto_scale));
    seed = hash_combine(seed, hash_value(text.style.min_size));
    seed = hash_combine(seed, hash_value(text.style.max_size));
    seed = hash_combine(seed, hash_value(text.box.size.x));
    seed = hash_combine(seed, hash_value(text.box.size.y));
    seed = hash_combine(seed, hash_value(text.box.enabled));
    return seed;
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

} // namespace

class SoftwareTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("text_render", trace_category::kText);
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;
        const float effective_size = node.shape.text.style.size;

        // Determine if geometric transform should be used (non-trivial affine transform)
        const bool use_geo_transform = !state.projection.ready &&
                                       is_affine_transform(model) &&
                                       has_non_translation(model);

        const Mat4* raster_transform = nullptr;
        Mat4 text_model_storage;

        if (use_geo_transform) {
            text_model_storage = model;
            raster_transform = &text_model_storage;
        }

        bool raster_cache_hit = false;
        auto raster = rasterize_text_to_bl_image(node.shape.text, effective_size, 4, &raster_cache_hit, raster_transform);
        if (!raster) {
            spdlog::warn("Text rasterization failed for node '{}'", node.name);
            return;
        }

        if (!raster_cache_hit) {
            renderer.counters()->text_glyphs_rasterized.fetch_add(
                static_cast<uint64_t>(node.shape.text.text.length()),
                std::memory_order_relaxed
            );
        }

        // 1. Drop Shadows (behind)
        for (size_t i = 0; i < node.shape.text.style.shadows.size(); ++i) {
            const auto& shadow = node.shape.text.style.shadows[i];
            if (shadow.enabled && shadow.opacity > 0.0f && shadow.color.a > 0.0f) {
                draw_text_shadow(renderer, fb, node, state, *raster, shadow, i);
            }
        }

        // 2. Glow
        if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
            draw_text_glow(renderer, fb, node, state, *raster);
        }

        // 3. Text
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster->x_offset));
            int y = static_cast<int>(std::lround(raster->y_offset));
            blend2d_bridge::composite_bl_image(fb, raster->image, x, y, opacity, BlendMode::Normal);
        } else if (state.projection.ready) {
            const int x = static_cast<int>(std::lround(model[3][0] + raster->x_offset));
            const int y = static_cast<int>(std::lround(model[3][1] + raster->y_offset));
            blend2d_bridge::composite_bl_image(fb, raster->image, x, y, opacity, BlendMode::Normal);
        } else {
            Mat4 text_model = model * glm::translate(Mat4(1.0f), Vec3(raster->x_offset, raster->y_offset, 0.0f));
            blend2d_bridge::composite_bl_image_transformed(fb, raster->image, text_model, opacity, BlendMode::Normal);
        }
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        const auto& txt = shape.text;
        f32 w = 400.0f;
        f32 h = 200.0f;
        if (txt.box.enabled && txt.box.size.x > 0.0f && txt.box.size.y > 0.0f) {
            w = txt.box.size.x;
            h = txt.box.size.y;
        }

        Vec4 corners[4] = {
            model * Vec4(0.0f, 0.0f, 0.0f, 1.0f),
            model * Vec4(w,    0.0f, 0.0f, 1.0f),
            model * Vec4(w,    h,    0.0f, 1.0f),
            model * Vec4(0.0f, h,    0.0f, 1.0f)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        for (const auto& c : corners) {
            min_x = std::min(min_x, c.x);
            max_x = std::max(max_x, c.x);
            min_y = std::min(min_y, c.y);
            max_y = std::max(max_y, c.y);
        }

        const f32 pad = spread + 20.0f;

        return {
            static_cast<i32>(std::floor(min_x - pad)),
            static_cast<i32>(std::floor(min_y - pad)),
            static_cast<i32>(std::ceil(max_x + pad)),
            static_cast<i32>(std::ceil(max_y + pad))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }

private:
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
            if (cached) shadow_cache = *cached;
        }

        if (!shadow_cache) {
            BLImage shadow_img;
            shadow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
            {
                BLContext ctx(shadow_img);
                ctx.clearAll();
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
                // Blur requires pixel-level operations, so we use the FB path via renderer
                // This is the one case where double conversion is unavoidable
                auto shadow_fb = std::make_shared<Framebuffer>(shadow_img.width(), shadow_img.height());
                shadow_fb->clear(Color::transparent());
                blend2d_bridge::composite_bl_image(*shadow_fb, shadow_img, 0, 0, 1.0f, BlendMode::Normal);
                renderer.apply_blur(*shadow_fb, shadow.blur);
                
                // For simplicity when blur is needed, just don't cache as BLImage
                // The shadow/glow effect with blur will not be cached to avoid complexity
                cached_img = nullptr; // Signal no caching for blurry shadows
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
            } else if (state.projection.ready) {
                const int x = static_cast<int>(std::lround(model[3][0] + raster.x_offset + shadow.offset.x));
                const int y = static_cast<int>(std::lround(model[3][1] + raster.y_offset + shadow.offset.y));
                blend2d_bridge::composite_bl_image(fb, *shadow_cache, x, y, opacity * shadow_opacity, BlendMode::Normal);
            } else {
                Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
                blend2d_bridge::composite_bl_image_transformed(fb, *shadow_cache, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
            }
        } else if (shadow.blur > 0.0f) {
            // Fallback for blurry shadows: render directly without caching
            BLImage shadow_img;
            shadow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
            {
                BLContext ctx(shadow_img);
                ctx.clearAll();
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
            
            auto shadow_fb = std::make_shared<Framebuffer>(shadow_img.width(), shadow_img.height());
            shadow_fb->clear(Color::transparent());
            blend2d_bridge::composite_bl_image(*shadow_fb, shadow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*shadow_fb, shadow.blur);
            
            if (use_geo_transform) {
                int x = static_cast<int>(std::lround(raster.x_offset + shadow.offset.x));
                int y = static_cast<int>(std::lround(raster.y_offset + shadow.offset.y));
                blend2d_bridge::composite_framebuffer(fb, *shadow_fb, x, y, opacity * shadow_opacity, BlendMode::Normal);
            } else if (state.projection.ready) {
                const int x = static_cast<int>(std::lround(model[3][0] + raster.x_offset + shadow.offset.x));
                const int y = static_cast<int>(std::lround(model[3][1] + raster.y_offset + shadow.offset.y));
                blend2d_bridge::composite_framebuffer(fb, *shadow_fb, x, y, opacity * shadow_opacity, BlendMode::Normal);
            } else {
                Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
                blend2d_bridge::composite_framebuffer_transformed(fb, *shadow_fb, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
            }
        }
    }

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
            if (cached) glow_cache = *cached;
        }

        if (!glow_cache) {
            BLImage glow_img;
            glow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
            {
                BLContext ctx(glow_img);
                ctx.clearAll();
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
            } else if (state.projection.ready) {
                const int x = static_cast<int>(std::lround(model[3][0] + raster.x_offset));
                const int y = static_cast<int>(std::lround(model[3][1] + raster.y_offset));
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
                ctx.clearAll();
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
            } else if (state.projection.ready) {
                const int x = static_cast<int>(std::lround(model[3][0] + raster.x_offset));
                const int y = static_cast<int>(std::lround(model[3][1] + raster.y_offset));
                blend2d_bridge::composite_framebuffer(fb, *glow_fb, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
            } else {
                Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
                blend2d_bridge::composite_framebuffer_transformed(fb, *glow_fb, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
            }
        }
    }
};

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    get_glow_cache().clear();
}

void clear_text_shadow_cache() {
    std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
    get_shadow_cache().clear();
}

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
