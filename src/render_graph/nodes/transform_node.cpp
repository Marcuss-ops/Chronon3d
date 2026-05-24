#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <span>

// Internal helpers extracted into separate compilation units
#include "transform_internal.hpp"

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> TransformNode::execute(
    RenderGraphContext& ctx,
    std::span<const std::shared_ptr<Framebuffer>> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    CHRONON_ZONE_C("transform_node", trace_category::kRasterize);
    if (ctx.counters) {
        ctx.counters->transform_calls.fetch_add(1, std::memory_order_relaxed);
    }

    if (inputs.empty() || !inputs[0]) {
        return ctx.acquire_framebuffer(ctx.width, ctx.height);
    }

    auto input = inputs[0];
    const auto predicted = predicted_bbox(ctx, input_bboxes);
    const raster::BBox out_bounds = predicted.value_or(raster::BBox{0, 0, ctx.width, ctx.height});
    const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
    const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);

    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const f32 opacity = m_use_matrix ? m_opacity : m_transform.opacity;

    // ── Identity fast-path ──────────────────────────────────────────────
    bool is_identity = true;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float expected = (i == j) ? 1.0f : 0.0f;
            if (std::abs(model[i][j] - expected) > 1e-6f) {
                is_identity = false;
                break;
            }
        }
        if (!is_identity) break;
    }

    if (is_identity && std::abs(opacity - 1.0f) < 1e-6f &&
        input->width() == out_w && input->height() == out_h &&
        input->origin_x() == out_bounds.x0 && input->origin_y() == out_bounds.y0) {
        const uint64_t area = static_cast<uint64_t>(out_w) * out_h;
        if (ctx.backend) {
            ctx.backend->counters()->pixels_touched.fetch_add(area, std::memory_order_relaxed);
        }
        if (ctx.counters) {
            ctx.counters->transform_pixels.fetch_add(area, std::memory_order_relaxed);
        }
        if (input.use_count() == 1) {
            return input;
        } else {
            auto result = ctx.acquire_framebuffer(out_w, out_h, true, out_bounds);
            std::copy(input->data(), input->data() + input->pixel_count(), result->data());
            result->set_opaque(input->is_opaque());
            return result;
        }
    }

    auto result = ctx.acquire_framebuffer(out_w, out_h, true, out_bounds);

    // ── Centering & homography ──────────────────────────────────────────
    const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const Mat4 src_canvas_offset = math::translate(Vec3(input->width() * 0.5f, input->height() * 0.5f, 0.0f));
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    f32 x_min_src = 0.0f;
    f32 y_min_src = 0.0f;
    f32 x_max_src = static_cast<f32>(input->width());
    f32 y_max_src = static_cast<f32>(input->height());
    if (!input_bboxes.empty() && input_bboxes[0].has_value()) {
        const auto& in_box = *input_bboxes[0];
        const i32 local_x0 = in_box.x0 - input->origin_x();
        const i32 local_y0 = in_box.y0 - input->origin_y();
        const i32 local_x1 = in_box.x1 - input->origin_x();
        const i32 local_y1 = in_box.y1 - input->origin_y();
        x_min_src = static_cast<f32>(std::clamp(local_x0, 0, input->width()));
        y_min_src = static_cast<f32>(std::clamp(local_y0, 0, input->height()));
        x_max_src = static_cast<f32>(std::clamp(local_x1, 0, input->width()));
        y_max_src = static_cast<f32>(std::clamp(local_y1, 0, input->height()));
        if (x_min_src >= x_max_src || y_min_src >= y_max_src) {
            return result;
        }
    }

    const f32 w_src = x_max_src - x_min_src;
    const f32 h_src = y_max_src - y_min_src;

    // Extract 3×3 homography for local_z = 0 plane
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

    // ── Projected quad → destination bounding box ───────────────────────
    Vec4 corners[4] = {
        pixel_model * Vec4(x_min_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_max_src, 0, 1),
        pixel_model * Vec4(x_min_src, y_max_src, 0, 1)
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

    i32 x0 = std::clamp(static_cast<i32>(std::floor(min_x)), result->origin_x(), result->origin_x() + result->width());
    i32 x1 = std::clamp(static_cast<i32>(std::ceil(max_x)), result->origin_x(), result->origin_x() + result->width());
    i32 y0 = std::clamp(static_cast<i32>(std::floor(min_y)), result->origin_y(), result->origin_y() + result->height());
    i32 y1 = std::clamp(static_cast<i32>(std::ceil(max_y)), result->origin_y(), result->origin_y() + result->height());

    if (ctx.clip_rect) {
        x0 = std::max(x0, ctx.clip_rect->x0);
        y0 = std::max(y0, ctx.clip_rect->y0);
        x1 = std::min(x1, ctx.clip_rect->x1);
        y1 = std::min(y1, ctx.clip_rect->y1);
    }

    const uint64_t area = (x1 > x0 && y1 > y0)
        ? static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0)
        : 0;

    if (ctx.backend) {
        ctx.backend->counters()->pixels_touched.fetch_add(area, std::memory_order_relaxed);
    }
    if (ctx.counters) {
        ctx.counters->transform_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    // ── Dispatch: choose the right transform path ───────────────────────

    // Fast-path 1: pure translation (identity 2×2 + no perspective)
    const bool is_pure_translation =
        std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[0][1]) < 1e-6f && std::abs(H[0][2]) < 1e-6f &&
        std::abs(H[1][0]) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f && std::abs(H[1][2]) < 1e-6f &&
        std::abs(H[2][2] - 1.0f) < 1e-6f;

    if (is_pure_translation) {
        const f32 tx = H[2][0];
        const f32 ty = H[2][1];
        const bool is_integer_translation =
            std::abs(tx - std::round(tx)) < 1e-4f &&
            std::abs(ty - std::round(ty)) < 1e-4f;

        if (is_integer_translation) {
            const i32 itx = static_cast<i32>(std::round(tx));
            const i32 ity = static_cast<i32>(std::round(ty));
            detail::execute_translate_clamped(
                result.get(), input.get(),
                x0, x1, y0, y1,
                x_min_src, x_max_src, y_min_src, y_max_src,
                itx, ity, opacity);
            return result;
        }
    }

    // Fast-path 2: affine + no scale/shear (effectively translation)
    const bool is_affine = std::abs(H[2][0]) < 1e-9f && std::abs(H[2][1]) < 1e-9f;

    if (is_affine && std::abs(H[0][1]) < 1e-6f && std::abs(H[1][0]) < 1e-6f &&
        std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f) {
        const float tx = H[2][0];
        const float ty = H[2][1];
        if (std::abs(tx - std::round(tx)) < 1e-4f && std::abs(ty - std::round(ty)) < 1e-4f) {
            const i32 itx = static_cast<i32>(std::round(tx));
            const i32 ity = static_cast<i32>(std::round(ty));
            detail::execute_translate_memcpy(
                result.get(), input.get(),
                x0, x1, y0, y1,
                itx, ity, opacity);
            return result;
        }
    }

    // General path: affine or projective, row-based
    const Vec3 h_col_start = inv_pixel_model_3x3 * Vec3(static_cast<f32>(x0) + 0.5f, static_cast<f32>(y0) + 0.5f, 1.0f);
    const Vec3 h_step_x = inv_pixel_model_3x3[0];
    const Vec3 h_step_y = inv_pixel_model_3x3[1];

    if (is_affine) {
        const f32 inv_z = 1.0f / h_col_start.z;
        const f32 dsx = h_step_x.x * inv_z;
        const f32 dsy = h_step_x.y * inv_z;

        auto worker = [&](i32 row_begin, i32 row_end) {
            detail::execute_affine_rows(
                result.get(), input.get(),
                x0, x1, y0,
                x_min_src, x_max_src, y_min_src, y_max_src,
                opacity, m_mode,
                h_col_start, h_step_y, inv_z, dsx, dsy,
                row_begin, row_end);
        };
        if (y1 - y0 >= 64 && area >= 128 * 128) {
            tbb::parallel_for(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) { worker(range.begin(), range.end()); });
        } else {
            worker(y0, y1);
        }
    } else {
        auto worker = [&](i32 row_begin, i32 row_end) {
            detail::execute_projective_rows(
                result.get(), input.get(),
                x0, x1, y0,
                x_min_src, x_max_src, y_min_src, y_max_src,
                opacity, m_mode,
                h_col_start, h_step_x, h_step_y,
                row_begin, row_end);
        };
        if (y1 - y0 >= 64 && area >= 128 * 128) {
            tbb::parallel_for(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) { worker(range.begin(), range.end()); });
        } else {
            worker(y0, y1);
        }
    }

    return result;
}

std::optional<raster::BBox> TransformNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

    f32 x_min_src = 0.0f;
    f32 y_min_src = 0.0f;
    f32 x_max_src = static_cast<f32>(ctx.width);
    f32 y_max_src = static_cast<f32>(ctx.height);
    if (!input_bboxes.empty() && input_bboxes[0].has_value()) {
        const auto& in_box = *input_bboxes[0];
        x_min_src = static_cast<f32>(std::clamp(in_box.x0, 0, ctx.width));
        y_min_src = static_cast<f32>(std::clamp(in_box.y0, 0, ctx.height));
        x_max_src = static_cast<f32>(std::clamp(in_box.x1, 0, ctx.width));
        y_max_src = static_cast<f32>(std::clamp(in_box.y1, 0, ctx.height));
        if (x_min_src >= x_max_src || y_min_src >= y_max_src) {
            const i32 empty_x = static_cast<i32>(std::floor(x_min_src));
            const i32 empty_y = static_cast<i32>(std::floor(y_min_src));
            return raster::BBox{empty_x, empty_y, empty_x, empty_y};
        }
    }

    const Mat4 src_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    Vec4 corners[4] = {
        pixel_model * Vec4(x_min_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_max_src, 0, 1),
        pixel_model * Vec4(x_min_src, y_max_src, 0, 1)
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
