#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/profiling.hpp>
#include <spdlog/spdlog.h>
#include "../utils/render_effects_processor.hpp"
#include "../utils/blend2d_bridge.hpp"
#include "../utils/blend2d_resources.hpp"
#include <chronon3d/core/counters.hpp>
#include <blend2d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>
#include <unordered_map>

namespace chronon3d::renderer {

namespace {

using CacheKey = u64;

CacheKey hash_combine(CacheKey seed, CacheKey value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

template <typename T>
CacheKey hash_value(const T& value) {
    return static_cast<CacheKey>(std::hash<T>{}(value));
}

CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    CacheKey seed = 0;
    seed = hash_combine(seed, hash_value(text.text));
    seed = hash_combine(seed, hash_value(text.style.font_path));
    seed = hash_combine(seed, hash_value(text.style.font_family));
    seed = hash_combine(seed, hash_value(text.style.font_weight));
    seed = hash_combine(seed, hash_value(text.style.font_style));
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

std::unordered_map<CacheKey, std::shared_ptr<Framebuffer>> g_text_glow_cache;
std::mutex g_text_glow_cache_mutex;

std::unordered_map<CacheKey, std::shared_ptr<Framebuffer>> g_text_shadow_cache;
std::mutex g_text_shadow_cache_mutex;

} // namespace

class SoftwareTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("text_render", trace_category::kText);
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;

        const float effective_size = node.shape.text.style.size;

        bool raster_cache_hit = false;
        // Rasterize once and reuse the cached atlas when the text content is unchanged.
        auto raster = rasterize_text_to_bl_image(node.shape.text, effective_size, 4, &raster_cache_hit);
        if (!raster) {
            spdlog::warn("Text rasterization failed for node '{}'", node.name);
            return;
        }

        if (!raster_cache_hit) {
            // Count only the frames that actually had to rasterize glyphs.
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
        if (state.projection.ready) {
            const int x = static_cast<int>(std::lround(model[3][0] + raster->x_offset));
            const int y = static_cast<int>(std::lround(model[3][1] + raster->y_offset));
            blend2d_bridge::composite_bl_image(fb, raster->image, x, y, opacity, BlendMode::Normal);
        } else {
            // Use the transformed compositor when no projection pass is active.
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

        // Corners in local space
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

        const CacheKey key = hash_shadow_params(node, node.shape.text.style.size, index);
        std::shared_ptr<Framebuffer> shadow_cache;
        {
            std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
            auto it = g_text_shadow_cache.find(key);
            if (it != g_text_shadow_cache.end()) {
                shadow_cache = it->second;
            }
        }

        if (!shadow_cache) {
            // Apply shadow color to copy of raster image
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

            auto cached_fb = std::make_shared<Framebuffer>(shadow_img.width(), shadow_img.height());
            cached_fb->clear(Color::transparent());
            blend2d_bridge::composite_bl_image(*cached_fb, shadow_img, 0, 0, 1.0f, BlendMode::Normal);

            if (shadow.blur > 0.0f) {
                renderer.apply_blur(*cached_fb, shadow.blur);
            }

            {
                std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
                shadow_cache = g_text_shadow_cache.emplace(key, std::move(cached_fb)).first->second;
            }
        }

        const f32 shadow_opacity = shadow.opacity * shadow.color.a;

        if (state.projection.ready) {
            const int x = static_cast<int>(std::lround(model[3][0] + raster.x_offset + shadow.offset.x));
            const int y = static_cast<int>(std::lround(model[3][1] + raster.y_offset + shadow.offset.y));
            blend2d_bridge::composite_framebuffer(fb, *shadow_cache, x, y, opacity * shadow_opacity, BlendMode::Normal);
        } else {
            // Use transformed compositor to follow perspective
            Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
            blend2d_bridge::composite_framebuffer_transformed(fb, *shadow_cache, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
        }
    }

    void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, const TextRasterization& raster) {
        CHRONON_ZONE_C("text_glow", trace_category::kText);
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;

        const CacheKey key = hash_glow_params(node, node.shape.text.style.size);
        std::shared_ptr<Framebuffer> glow_cache;
        {
            std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
            auto it = g_text_glow_cache.find(key);
            if (it != g_text_glow_cache.end()) {
                glow_cache = it->second;
            }
        }

        if (!glow_cache) {
            // Apply glow color to a copy of the text image
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

            auto cached_fb = std::make_shared<Framebuffer>(glow_img.width(), glow_img.height());
            cached_fb->clear(Color::transparent());
            blend2d_bridge::composite_bl_image(*cached_fb, glow_img, 0, 0, 1.0f, BlendMode::Normal);

            if (node.glow.radius > 0.0f) {
                renderer.apply_blur(*cached_fb, node.glow.radius);
            }

            {
                std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
                glow_cache = g_text_glow_cache.emplace(key, std::move(cached_fb)).first->second;
            }
        }

        const f32 glow_intensity_opacity = node.glow.intensity * node.glow.color.a;

        if (state.projection.ready) {
            const int x = static_cast<int>(std::lround(model[3][0] + raster.x_offset));
            const int y = static_cast<int>(std::lround(model[3][1] + raster.y_offset));
            blend2d_bridge::composite_framebuffer(fb, *glow_cache, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
        } else {
            // Use transformed compositor to follow perspective
            Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
            
            blend2d_bridge::composite_framebuffer_transformed(fb, *glow_cache, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
        }
    }
};

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    g_text_glow_cache.clear();
}

void clear_text_shadow_cache() {
    std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
    g_text_shadow_cache.clear();
}

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
