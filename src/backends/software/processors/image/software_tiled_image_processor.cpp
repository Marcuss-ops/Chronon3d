#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "../../utils/render_effects_processor.hpp"
// R2: draw() now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>

namespace chronon3d::renderer {

class SoftwareTiledImageProcessor final : public ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("tiled_image_render", trace_category::kImage);
        rctx.counters->images_sampled.fetch_add(1, std::memory_order_relaxed);

        draw_shadow(fb, node, state);
        rctx.image_renderer->draw_image_tiled(node.shape.image(), state, fb);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        // Tiled image is assumed to cover the entire viewport
        return { -1000000, -1000000, 1000000, 1000000 };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_tiled_image_processor() {
    return std::make_unique<SoftwareTiledImageProcessor>();
}

} // namespace chronon3d::renderer
