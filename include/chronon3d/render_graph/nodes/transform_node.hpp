#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <glm/glm.hpp>

namespace chronon3d::graph {

/**
 * TransformNode applies a spatial transformation to an input framebuffer.
 * Used for layer-level transforms and precompositions.
 */
class TransformNode final : public RenderGraphNode {
public:
    explicit TransformNode(Transform transform) : m_transform(std::move(transform)) {}

    [[nodiscard]] RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Transform; }
    [[nodiscard]] std::string name() const override { return "Transform"; }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "transform",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = rendergraph::hash_transform(m_transform)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty() || !inputs[0]) {
            auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
            fb->clear(Color::transparent());
            return fb;
        }

        auto input = inputs[0];
        auto result = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        result->clear(Color::transparent());

        // Centering logic: both source and destination framebuffers are centered at (0,0) in scene space.
        const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 src_canvas_offset = math::translate(Vec3(input->width() * 0.5f, input->height() * 0.5f, 0.0f));
        
        // Final pixel matrix: DstPixel <- DstScene <- SrcScene <- SrcPixel
        const Mat4 pixel_model = dst_canvas_offset * m_transform.to_mat4() * glm::inverse(src_canvas_offset);
        const Mat4 inv_model = glm::inverse(pixel_model);

        // Bounding box for optimization in destination pixels
        Vec4 corners[4] = {
            pixel_model * Vec4(0, 0, 0, 1),
            pixel_model * Vec4(static_cast<f32>(input->width()), 0, 0, 1),
            pixel_model * Vec4(static_cast<f32>(input->width()), static_cast<f32>(input->height()), 0, 1),
            pixel_model * Vec4(0, static_cast<f32>(input->height()), 0, 1)
        };

        f32 min_x = corners[0].x, max_x = corners[0].x;
        f32 min_y = corners[0].y, max_y = corners[0].y;
        for (int i = 1; i < 4; ++i) {
            min_x = std::min(min_x, corners[i].x); max_x = std::max(max_x, corners[i].x);
            min_y = std::min(min_y, corners[i].y); max_y = std::max(max_y, corners[i].y);
        }

        const i32 x0 = std::max<i32>(0, static_cast<i32>(std::floor(min_x)));
        const i32 y0 = std::max<i32>(0, static_cast<i32>(std::floor(min_y)));
        const i32 x1 = std::min<i32>(ctx.width,  static_cast<i32>(std::ceil(max_x)));
        const i32 y1 = std::min<i32>(ctx.height, static_cast<i32>(std::ceil(max_y)));

        if (x0 >= x1 || y0 >= y1) return result;

        for (i32 y = y0; y < y1; ++y) {
            Color* dst_row = result->pixels_row(y);
            for (i32 x = x0; x < x1; ++x) {
                Vec4 local = inv_model * Vec4(static_cast<f32>(x) + 0.5f,
                                              static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);

                if (local.x < 0.0f || local.y < 0.0f ||
                    local.x >= static_cast<f32>(input->width()) || 
                    local.y >= static_cast<f32>(input->height())) {
                    continue;
                }

                // Nearest neighbor sampling
                int sx = static_cast<int>(local.x);
                int sy = static_cast<int>(local.y);
                
                Color src = input->get_pixel(sx, sy);
                src.a *= m_transform.opacity;


                if (src.a <= 0.0f) continue;
                dst_row[x] = src; 
            }
        }

        return result;
    }

private:
    Transform m_transform;
};

} // namespace chronon3d::graph
