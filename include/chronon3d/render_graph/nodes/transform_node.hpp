#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

/**
 * TransformNode applies a spatial transformation to an input framebuffer.
 * Used for layer-level transforms and precompositions.
 */
class TransformNode final : public RenderGraphNode {
public:
    explicit TransformNode(Transform transform, SamplingMode mode = SamplingMode::Bilinear) 
        : m_transform(std::move(transform)), m_mode(mode), m_use_matrix(false) {}

    explicit TransformNode(Mat4 matrix, f32 opacity = 1.0f, SamplingMode mode = SamplingMode::Bilinear)
        : m_matrix(matrix), m_opacity(opacity), m_mode(mode), m_use_matrix(true) {}

    [[nodiscard]] RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Transform; }
    [[nodiscard]] std::string name() const override { return "Transform"; }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        u64 params_hash = hash_combine(
            hash_transform(m_transform),
            static_cast<u64>(m_mode)
        );

        if (m_use_matrix) {
            params_hash = hash_combine(params_hash, hash_bytes(&m_matrix, sizeof(m_matrix)));
        }

        return cache::NodeCacheKey{
            .scope = "transform",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = params_hash
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
        const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
        const f32 opacity = m_use_matrix ? m_opacity : m_transform.opacity;
        const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);


        // Extract 3x3 homography for local_z = 0 plane
        // M maps (x, y, 0, 1) -> (px, py, pz, pw).
        // H maps (x, y, 1) -> (px, py, pw).
        glm::mat3 H;
        H[0][0] = pixel_model[0][0]; H[0][1] = pixel_model[0][1]; H[0][2] = pixel_model[0][3];
        H[1][0] = pixel_model[1][0]; H[1][1] = pixel_model[1][1]; H[1][2] = pixel_model[1][3];
        H[2][0] = pixel_model[3][0]; H[2][1] = pixel_model[3][1]; H[2][2] = pixel_model[3][3];

        const glm::mat3 inv_pixel_model_3x3 = glm::inverse(H);

        const f32 w_src = static_cast<f32>(input->width());
        const f32 h_src = static_cast<f32>(input->height());

        // Bounding box for optimization in destination pixels
        // We use the 4 corners of the source image in scene space.
        Vec4 corners[4] = {
            pixel_model * Vec4(0, 0, 0, 1),
            pixel_model * Vec4(w_src, 0, 0, 1),
            pixel_model * Vec4(w_src, h_src, 0, 1),
            pixel_model * Vec4(0, h_src, 0, 1)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        for (auto& c : corners) {
            if (std::abs(c.w) < 1e-6f) continue;
            f32 px = c.x / c.w;
            f32 py = c.y / c.w;
            min_x = std::min(min_x, px);
            max_x = std::max(max_x, px);
            min_y = std::min(min_y, py);
            max_y = std::max(max_y, py);
        }

        i32 x0 = std::clamp(static_cast<i32>(std::floor(min_x)), 0, ctx.width);
        i32 x1 = std::clamp(static_cast<i32>(std::ceil(max_x)), 0, ctx.width);
        i32 y0 = std::clamp(static_cast<i32>(std::floor(min_y)), 0, ctx.height);
        i32 y1 = std::clamp(static_cast<i32>(std::ceil(max_y)), 0, ctx.height);

        for (i32 y = y0; y < y1; ++y) {
            Color* dst_row = result->pixels_row(y);
            for (i32 x = x0; x < x1; ++x) {
                // Map screen pixel (x,y) back to source pixel (sx,sy)
                Vec3 src_p_h = inv_pixel_model_3x3 * Vec3(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 1.0f);
                if (std::abs(src_p_h.z) < 1e-9f) continue;
                
                const f32 sx = src_p_h.x / src_p_h.z;
                const f32 sy = src_p_h.y / src_p_h.z;

                if (sx >= 0 && sx < w_src && sy >= 0 && sy < h_src) {
                    Color src = input->sample(sx, sy, m_mode);
                    if (src.a > 0.01f) {
                        src.r *= opacity;
                        src.g *= opacity;
                        src.b *= opacity;
                        src.a *= opacity;
                        dst_row[x] = src;
                    }
                }
            }
        }

        return result;
    }

private:
    Transform m_transform;
    Mat4      m_matrix{1.0f};
    f32       m_opacity{1.0f};
    SamplingMode m_mode{SamplingMode::Bilinear};
    bool      m_use_matrix{false};
};

} // namespace chronon3d::graph
