#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/scene/model/render/render_runtime.hpp>
#include "../rasterizers/shape_rasterizer.hpp"
#include "../primitive_renderer.hpp"

namespace chronon3d::renderer {

class SoftwareFakeBox3DProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        FakeBox3DRenderState s;
        if (auto* ptr = std::get_if<FakeBox3DRenderState>(&node.params)) {
            s = *ptr;
        }
        // Prefer the camera_2_5d-based projection context (populated from scene camera)
        if (!s.projection.ready && state.projection.ready) {
            s.projection = state.projection;
        } else if (!s.projection.ready) {
            prepare_projection_context(s, camera, width, height);
        }
        s.world_matrix = state.world_matrix;
        chronon3d::renderer::draw_fake_box3d(fb, node, state, node.shape.fake_box3d, s);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return renderer::compute_world_bbox(shape, model, spread);
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

class SoftwareGridPlaneProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        GridPlaneRenderState s;
        if (auto* ptr = std::get_if<GridPlaneRenderState>(&node.params)) {
            s = *ptr;
        }
        if (!s.projection.ready && state.projection.ready) {
            s.projection = state.projection;
        } else if (!s.projection.ready) {
            prepare_projection_context(s, camera, width, height);
        }
        chronon3d::renderer::draw_grid_plane(fb, node, state, node.shape.grid_plane, s);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return renderer::compute_world_bbox(shape, model, spread);
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_fake_box3d_processor() {
    return std::make_unique<SoftwareFakeBox3DProcessor>();
}

std::unique_ptr<ShapeProcessor> create_grid_plane_processor() {
    return std::make_unique<SoftwareGridPlaneProcessor>();
}

} // namespace chronon3d::renderer
