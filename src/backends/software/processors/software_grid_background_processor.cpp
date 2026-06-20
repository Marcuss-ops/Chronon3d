#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include "src/backends/software/kernels/grid_background_kernel.hpp"

namespace chronon3d::renderer {

class SoftwareGridBackgroundProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        (void)renderer;
        (void)camera;
        (void)width;
        (void)height;

        const auto& g = node.shape.grid_background();
        if (g.size.x <= 0.0f || g.size.y <= 0.0f) {
            return;
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
