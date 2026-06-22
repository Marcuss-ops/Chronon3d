#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "../../utils/render_effects_processor.hpp"
#include <spdlog/spdlog.h>
// R2: draw() now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>

namespace chronon3d::renderer {

class SoftwareImageProcessor final : public ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("image_render", trace_category::kImage);
        // Increment images sampled counter
        rctx.counters->images_sampled.fetch_add(1, std::memory_order_relaxed);

        draw_shadow(fb, node, state);
        if (node.glow.enabled) {
            const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
            const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
            const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
            const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
            spdlog::info(
                "[image-processor] node='{}' layer='{}' glow radius={:.2f} intensity={:.3f} color=({:.2f},{:.2f},{:.2f},{:.2f}) clip={} clip_rect=[{},{} -> {},{}]",
                node.name,
                state.layer_id,
                node.glow.radius,
                node.glow.intensity,
                node.glow.color.r,
                node.glow.color.g,
                node.glow.color.b,
                node.glow.color.a,
                state.clip_rect ? 1 : 0,
                clip_x0,
                clip_y0,
                clip_x1,
                clip_y1
            );
            draw_glow(fb, node, state);
        }
        rctx.image_renderer->draw_image(node.shape.image(), state, fb);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        const auto& img = shape.image();
        const f32 w = img.size.x;
        const f32 h = img.size.y;
        const f32 pad = spread + kBBoxSafetyPadding;
        
        // Corners in local space (centered around 0,0)
        Vec4 corners[4] = {
            model * Vec4(-pad, -pad, 0, 1),
            model * Vec4(w + pad, -pad, 0, 1),
            model * Vec4(w + pad, h + pad, 0, 1),
            model * Vec4(-pad, h + pad, 0, 1)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        for (const auto& c : corners) {
            min_x = std::min(min_x, c.x);
            max_x = std::max(max_x, c.x);
            min_y = std::min(min_y, c.y);
            max_y = std::max(max_y, c.y);
        }

        return {
            static_cast<i32>(std::floor(min_x)),
            static_cast<i32>(std::floor(min_y)),
            static_cast<i32>(std::ceil(max_x)),
            static_cast<i32>(std::ceil(max_y))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_image_processor() {
    return std::make_unique<SoftwareImageProcessor>();
}

} // namespace chronon3d::renderer
