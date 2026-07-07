#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include "../kernels/grid_background_kernel.hpp"
// R2: draw() now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>

namespace chronon3d::renderer {

class SoftwareGridBackgroundProcessor final : public ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        (void)rctx;
        (void)camera;
        (void)width;
        (void)height;

        auto g = node.shape.grid_background();
        if (g.size.x <= 0.0f || g.size.y <= 0.0f) {
            return;
        }

        // TICKET-122 FASE 6: apply projected matrix scale to grid geometry
        // so the grid scales with zoom.  The matrix includes canvas_center +
        // ssaa + 2.5D projection; we extract just the net scale factor.
        // Only apply when a 2.5D camera is active (state.projection.ready).
        if (state.projection.ready) {
            const f32 sx = state.matrix[0][0];
            if (sx > 0.0f) {
                g.spacing          *= sx;
                g.minor_thickness  *= sx;
                g.major_thickness  *= sx;
            }
        }

        // Always render full viewport — the clip_rect from the render system may
        // be a dirty rect union from other layers (e.g., text fade-in), and clipping
        // the grid to that region would chop off grid lines at the viewport edges.
        raster::BBox clip{0, 0, fb.width(), fb.height()};
        // Note: state.clip_rect is intentionally ignored for GridBackground.

        render_grid_background_kernel(fb, g, clip, state);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        (void)model;
        const auto& g = shape.grid_background();
        const f32 pad = std::max(0.0f, spread) + kBBoxSafetyPadding;
        return {
            static_cast<i32>(std::floor(-pad)),
            static_cast<i32>(std::floor(-pad)),
            static_cast<i32>(std::ceil(g.size.x + pad)),
            static_cast<i32>(std::ceil(g.size.y + pad))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        (void)shape;
        (void)local_point;
        (void)spread;
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_grid_background_processor() {
    return std::make_unique<SoftwareGridBackgroundProcessor>();
}

} // namespace chronon3d::renderer
