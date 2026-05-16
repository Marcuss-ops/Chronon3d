#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include "../rasterizers/shape_rasterizer.hpp"
#include "../utils/render_effects_processor.hpp"

namespace chronon3d::renderer {

class SoftwareShapeProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        // 3D card path: project the rect as a flat card in world space
        if (state.is_3d_layer && state.projection.ready &&
            node.shape.type == ShapeType::Rect) {
            const auto& rect = node.shape.rect;
            const Color color = node.color.to_linear();
            Color fill{color.r, color.g, color.b, color.a * state.opacity};
            auto card = state.projection.project_card(state.world_matrix, rect.size);
            fill_projected_card(fb, card, fill);
            return;
        }

        if (node.shadow.enabled)
            draw_shadow(fb, node, state);
        if (node.glow.enabled)
            draw_glow(fb, node, state);

        const Color linear_color = node.color.to_linear();
        Color fill_color = linear_color;
        fill_color.a *= state.opacity;

        draw_transformed_shape(fb, node.shape, state.matrix, fill_color, 0.0f, &state);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return chronon3d::renderer::compute_world_bbox(shape, model, spread);
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return chronon3d::renderer::hit_test(shape, local_point, spread);
    }
};

std::unique_ptr<ShapeProcessor> create_shape_processor() {
    return std::make_unique<SoftwareShapeProcessor>();
}

} // namespace chronon3d::renderer
