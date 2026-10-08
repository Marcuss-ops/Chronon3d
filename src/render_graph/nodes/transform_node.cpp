#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <span>
#include <array>
#include <bit>
#include <cstring>

#include "native_surface.hpp"
#include "transform_policy.hpp"

// Internal helpers extracted into separate compilation units
#include "transform_internal.hpp"

#include "transform_geometry_detail.hpp"

namespace chronon3d::graph {

std::optional<raster::BBox> TransformNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const auto expand_card_bounds = [&](raster::BBox bounds) {
        if (!m_card3d_material || !m_card3d_material->enabled) return bounds;
        const i32 pad = static_cast<i32>(std::ceil(std::max(0.0f,
            m_card3d_material->thickness_px +
            std::max(m_card3d_material->rim_light_intensity > 0.0f ? 2.0f : 0.0f,
                     m_card3d_material->edge_highlight_intensity > 0.0f ? 2.0f : 0.0f)))) + 2;
        return raster::BBox{
            std::max(0, bounds.x0 - pad), std::max(0, bounds.y0 - pad),
            std::min(ctx.frame_input.width, bounds.x1 + pad),
            std::min(ctx.frame_input.height, bounds.y1 + pad)};
    };

    if (!m_card3d_material && get_transform_type() == TransformType::PureTranslation &&
        !input_bboxes.empty() && input_bboxes[0].has_value()) {
        const auto& in_box = *input_bboxes[0];
        const i32 src_w = std::max(1, in_box.x1 - in_box.x0);
        const i32 src_h = std::max(1, in_box.y1 - in_box.y0);
        const f32 tx = model[3][0];
        const f32 ty = model[3][1];

        const i32 x0 = std::clamp(static_cast<i32>(std::floor(static_cast<f32>(in_box.x0) + tx)) - 1, 0, ctx.frame_input.width);
        const i32 y0 = std::clamp(static_cast<i32>(std::floor(static_cast<f32>(in_box.y0) + ty)) - 1, 0, ctx.frame_input.height);
        const i32 x1 = std::clamp(x0 + src_w + 2, 0, ctx.frame_input.width);
        const i32 y1 = std::clamp(y0 + src_h + 2, 0, ctx.frame_input.height);

        return expand_card_bounds(raster::BBox{x0, y0, x1, y1});
    }

    const Mat4 dst_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));

    const Vec2 source_size =
        (m_source_surface_size.x > 0.0f && m_source_surface_size.y > 0.0f)
            ? m_source_surface_size
            : Vec2{static_cast<f32>(ctx.frame_input.width),
                   static_cast<f32>(ctx.frame_input.height)};
    f32 x_min_src = 0.0f;
    f32 y_min_src = 0.0f;
    f32 x_max_src = source_size.x;
    f32 y_max_src = source_size.y;

    if (!input_bboxes.empty() && input_bboxes[0].has_value()) {
        // The graph bbox can describe media/source geometry rather than the
        // actual surface supplied to this node.  Keep prediction consistent
        // with execute(), which clips the source rectangle to the input
        // surface before sampling.  Otherwise an oversized bbox expands the
        // predicted output and makes opaque padding cover the composite.
        const auto source = detail::normalize_transform_source_rect(
            *input_bboxes[0], 0, 0,
            source_size.x, source_size.y);
        x_min_src = source.x0;
        y_min_src = source.y0;
        x_max_src = source.x1;
        y_max_src = source.y1;

    }

    const Mat4 src_canvas_offset = glm::translate(
        Mat4(1.0f),
        Vec3(source_size.x * 0.5f, source_size.y * 0.5f, 0.0f));
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    const std::array<Vec4, 4> corners = {
        pixel_model * Vec4(x_min_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_max_src, 0, 1),
        pixel_model * Vec4(x_min_src, y_max_src, 0, 1)
    };
    const auto clipped = detail::clip_quad_against_w(corners);

    f32 min_x = 1e10f, max_x = -1e10f;
    f32 min_y = 1e10f, max_y = -1e10f;
    for (int i = 0; i < clipped.count; ++i) {
        const auto& c = clipped.vertices[i];
        f32 px = c.x / c.w;
        f32 py = c.y / c.w;
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
    }

    if (clipped.count < 3) {
        return std::nullopt;
    }

    auto bounds = detail::clip_transform_bounds(
        min_x, min_y, max_x, max_y,
        0, 0, ctx.frame_input.width, ctx.frame_input.height, std::nullopt);
    if (!bounds) return std::nullopt;
    return expand_card_bounds(*bounds);
}

} // namespace chronon3d::graph
