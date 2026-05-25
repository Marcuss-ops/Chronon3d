#include "software_text_processor_internal.hpp"
#include "../utils/blend2d_bridge.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::renderer {

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
};

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
