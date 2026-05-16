#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "../primitive_renderer.hpp"

namespace chronon3d::renderer {

class SoftwareMeshProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        if (!node.mesh) return;
        
        const Color linear_color = node.color.to_linear();
        const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
        
        chronon3d::renderer::render_mesh_wireframe(
            fb, *node.mesh, state.matrix, camera.view_matrix(),
            camera.projection_matrix(aspect), linear_color);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return {-1000, -1000, 1000, 1000}; // Mesh bbox is handled by scene builder usually
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_mesh_processor() {
    return std::make_unique<SoftwareMeshProcessor>();
}

} // namespace chronon3d::renderer
