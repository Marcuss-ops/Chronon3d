#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/profiling.hpp>
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

std::unordered_map<CacheKey, std::shared_ptr<Framebuffer>> g_text_glow_cache;
std::mutex g_text_glow_cache_mutex;

} // namespace

class SoftwareTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("text_render", trace_category::kText);
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;

        const float effective_size = node.shape.text.style.size;
        
        // 1. Rasterize text once for both glow and main text
        auto raster = rasterize_text_to_bl_image(node.shape.text, effective_size);
        if (!raster) return;

        // Increment text glyphs counter
        renderer.counters()->text_glyphs_rasterized.fetch_add(
            static_cast<uint64_t>(node.shape.text.text.length()), 
            std::memory_order_relaxed
        );

        // 2. Glow
        if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
            draw_text_glow(renderer, fb, node, state, *raster);
        }

        // 3. Text
        // Apply text color to a copy of the raster
        BLImage text_img;
        text_img.create(raster->image.width(), raster->image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(text_img);
            ctx.clearAll();
            ctx.blitImage(BLPoint(0, 0), raster->image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(node.shape.text.style.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.shape.text.style.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.shape.text.style.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }

        // Use the transformed compositor to respect perspective/tilt
        Mat4 text_model = model * glm::translate(Mat4(1.0f), Vec3(raster->x_offset, raster->y_offset, 0.0f));
        
        blend2d_bridge::composite_bl_image_transformed(fb, text_img, text_model, opacity, BlendMode::Normal);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        // Simple approximation for text bbox
        Vec3 pos = Vec3(model[3]);
        return {
            static_cast<i32>(pos.x - 200),
            static_cast<i32>(pos.y - 100),
            static_cast<i32>(pos.x + 200),
            static_cast<i32>(pos.y + 100)
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }

private:
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

        // Use transformed compositor to follow perspective
        Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
        
        blend2d_bridge::composite_framebuffer_transformed(fb, *glow_cache, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
    }
};

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    g_text_glow_cache.clear();
}

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
