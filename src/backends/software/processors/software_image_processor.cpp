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
        const auto& img = shape.image;
        const f32 w = img.size.x;
        const f32 h = img.size.y;
        
        // Corners in local space (centered around 0,0)
        Vec4 corners[4] = {
            model * Vec4(0, 0, 0, 1),
            model * Vec4(w, 0, 0, 1),
            model * Vec4(w, h, 0, 1),
            model * Vec4(0, h, 0, 1)
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
            static_cast<i32>(std::floor(min_x - spread)),
            static_cast<i32>(std::floor(min_y - spread)),
            static_cast<i32>(std::ceil(max_x + spread)),
            static_cast<i32>(std::ceil(max_y + spread))
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
