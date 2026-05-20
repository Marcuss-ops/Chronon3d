#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/counters.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/math/camera_2_5d_projection.hpp>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> TransformNode::execute(
    RenderGraphContext& ctx,
    const std::vector<std::shared_ptr<Framebuffer>>& inputs,
    const std::vector<std::optional<raster::BBox>>& input_bboxes
) {
    CHRONON_ZONE_C("transform_node", trace_category::kRasterize);
    if (ctx.backend) {
        ctx.backend->counters()->pixels_touched.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
    }
    if (ctx.counters) {
        ctx.counters->transform_calls.fetch_add(1, std::memory_order_relaxed);
        ctx.counters->transform_pixels.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
    }

    if (inputs.empty() || !inputs[0]) {
        auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height);
        return fb;
    }

    auto input = inputs[0];
    auto result = ctx.acquire_framebuffer(ctx.width, ctx.height);
    result->clear(Color::transparent());

    // Centering logic: both source and destination framebuffers are centered at (0,0) in scene space.
    const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const Mat4 src_canvas_offset = math::translate(Vec3(input->width() * 0.5f, input->height() * 0.5f, 0.0f));
    
    // Final pixel matrix: DstPixel <- DstScene <- SrcScene <- SrcPixel
    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const f32 opacity = m_use_matrix ? m_opacity : m_transform.opacity;
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);
    const f32 w_src = static_cast<f32>(input->width());
    const f32 h_src = static_cast<f32>(input->height());

    // Extract 3x3 homography for local_z = 0 plane
    glm::mat3 H;
    H[0][0] = pixel_model[0][0]; H[0][1] = pixel_model[0][1]; H[0][2] = pixel_model[0][3];
    H[1][0] = pixel_model[1][0]; H[1][1] = pixel_model[1][1]; H[1][2] = pixel_model[1][3];
    H[2][0] = pixel_model[3][0]; H[2][1] = pixel_model[3][1]; H[2][2] = pixel_model[3][3];

    const glm::mat3 inv_pixel_model_3x3 = glm::inverse(H);

    const auto winding = projected_quad_signed_area(pixel_model, w_src, h_src);
    const bool flipped = winding.has_value() && *winding < 0.0f;
    if (ctx.counters && flipped) {
        ctx.counters->projected_winding_flips.fetch_add(1, std::memory_order_relaxed);
    }

    if (ctx.diagnostics_enabled) {
        const f32 det = glm::determinant(H);
        spdlog::info(
            "[transform-debug] node='{}' det={:.6f} winding={} H=[[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}]]",
            name(),
            det,
            winding.has_value() ? (*winding < 0.0f ? "flipped" : "ok") : "invalid",
            H[0][0], H[0][1], H[0][2],
            H[1][0], H[1][1], H[1][2],
            H[2][0], H[2][1], H[2][2]
        );
        if (flipped) {
            spdlog::warn("[transform-debug] node='{}' projected quad has negative winding", name());
        }
    }

    // Bounding box for optimization in destination pixels
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

    const bool is_affine = std::abs(H[2][0]) < 1e-9f && std::abs(H[2][1]) < 1e-9f;

    const Vec3 h_col_start = inv_pixel_model_3x3 * Vec3(static_cast<f32>(x0) + 0.5f, static_cast<f32>(y0) + 0.5f, 1.0f);
    const Vec3 h_step_x = inv_pixel_model_3x3[0];
    const Vec3 h_step_y = inv_pixel_model_3x3[1];

    if (is_affine) {
        const f32 inv_z = 1.0f / h_col_start.z;
        for (i32 y = y0; y < y1; ++y) {
            Color* dst_row = result->pixels_row(y);
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            for (i32 x = x0; x < x1; ++x) {
                const f32 sx = h_row.x * inv_z;
                const f32 sy = h_row.y * inv_z;
                if (sx >= 0 && sx < w_src && sy >= 0 && sy < h_src) {
                    Color src = input->sample(sx, sy, m_mode);
                    if (src.a > 0.001f) {
                        src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                        dst_row[x] = src;
                    }
                }
                h_row += h_step_x;
            }
        }
    } else {
        for (i32 y = y0; y < y1; ++y) {
            Color* dst_row = result->pixels_row(y);
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            for (i32 x = x0; x < x1; ++x) {
                if (std::abs(h_row.z) > 1e-9f) {
                    const f32 inv_z = 1.0f / h_row.z;
                    const f32 sx = h_row.x * inv_z;
                    const f32 sy = h_row.y * inv_z;
                    if (sx >= 0 && sx < w_src && sy >= 0 && sy < h_src) {
                        Color src = input->sample(sx, sy, m_mode);
                        if (src.a > 0.001f) {
                            src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                            dst_row[x] = src;
                        }
                    }
                }
                h_row += h_step_x;
            }
        }
    }

    return result;
}

std::optional<raster::BBox> TransformNode::predicted_bbox(const RenderGraphContext& ctx) const {
    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    
    // We assume input is same size as output for simplicity if we don't have it yet.
    // In many cases (text freezing), it is!
    const f32 w_src = static_cast<f32>(ctx.width);
    const f32 h_src = static_cast<f32>(ctx.height);
    const Mat4 src_canvas_offset = math::translate(Vec3(w_src * 0.5f, h_src * 0.5f, 0.0f));
    
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

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

    return raster::BBox{
        std::clamp(static_cast<i32>(std::floor(min_x)), 0, ctx.width),
        std::clamp(static_cast<i32>(std::floor(min_y)), 0, ctx.height),
        std::clamp(static_cast<i32>(std::ceil(max_x)), 0, ctx.width),
        std::clamp(static_cast<i32>(std::ceil(max_y)), 0, ctx.height)
    };
}

} // namespace chronon3d::graph
