#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "../primitive_renderer.hpp"

namespace chronon3d::renderer {

class SoftwareLineProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, 
              const Camera& camera, i32 width, i32 height) override {
        Vec4 p0 = state.matrix * Vec4(0, 0, 0, 1);
        Vec4 p1 = state.matrix * Vec4(node.shape.line.to, 1);
        Color col = node.color.to_linear();
        col.a *= state.opacity;
        bline(fb, Vec2(p0.x, p0.y), Vec2(p1.x, p1.y), col);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(shape.line.to, 1);
        return {
            static_cast<i32>(std::floor(std::min(p0.x, p1.x) - spread)),
            static_cast<i32>(std::floor(std::min(p0.y, p1.y) - spread)),
            static_cast<i32>(std::ceil(std::max(p0.x, p1.x) + spread)),
            static_cast<i32>(std::ceil(std::max(p0.y, p1.y) + spread))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false; 
    }
};

std::unique_ptr<ShapeProcessor> create_line_processor() {
    return std::make_unique<SoftwareLineProcessor>();
}

} // namespace chronon3d::renderer
