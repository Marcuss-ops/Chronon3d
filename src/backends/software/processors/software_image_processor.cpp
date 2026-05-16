#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "../utils/render_effects_processor.hpp"

namespace chronon3d::renderer {

class SoftwareImageProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        draw_shadow(fb, node, state);
        renderer.image_renderer().draw_image(node.shape.image, state, fb);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        // TODO: Proper image bbox
        Vec3 pos = Vec3(model[3]);
        return {
            static_cast<i32>(pos.x - 100),
            static_cast<i32>(pos.y - 100),
            static_cast<i32>(pos.x + 100),
            static_cast<i32>(pos.y + 100)
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
