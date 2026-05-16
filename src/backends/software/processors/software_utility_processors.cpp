#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "../primitive_renderer.hpp"

namespace chronon3d::renderer {

class SoftwareFakeExtrudedTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        renderer.fake_extruded_text_renderer().draw(
            fb, node, state, camera, width, height, renderer.text_renderer());
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return {-1000, -1000, 1000, 1000};
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

class SoftwareFakeBox3DProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        auto s = node.fake_box3d_runtime;
        if (!s.projection.ready) {
            prepare_projection_context(s, camera, width, height);
        }
        chronon3d::renderer::draw_fake_box3d(fb, node, state, node.shape.fake_box3d, s);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return {-1000, -1000, 1000, 1000};
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

class SoftwareGridPlaneProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        auto s = node.grid_plane_runtime;
        if (!s.projection.ready) {
            prepare_projection_context(s, camera, width, height);
        }
        chronon3d::renderer::draw_grid_plane(fb, node, state, node.shape.grid_plane, s);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return {-1000, -1000, 1000, 1000};
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_fake_extruded_text_processor() {
    return std::make_unique<SoftwareFakeExtrudedTextProcessor>();
}

std::unique_ptr<ShapeProcessor> create_fake_box3d_processor() {
    return std::make_unique<SoftwareFakeBox3DProcessor>();
}

std::unique_ptr<ShapeProcessor> create_grid_plane_processor() {
    return std::make_unique<SoftwareGridPlaneProcessor>();
}

} // namespace chronon3d::renderer
