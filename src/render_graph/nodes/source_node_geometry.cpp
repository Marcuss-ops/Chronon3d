// ═══════════════════════════════════════════════════════════════════════════
// source_node_geometry.cpp — bounds and cache-key queries for SourceNode.
//
// Split out of source_node.cpp: SourceNode::predicted_bbox,
// detail::preflight_diagnostic_bbox, detail::covers_full_frame_as_rectangle
// and SourceNode::cache_key.
// ═══════════════════════════════════════════════════════════════════════════

#include "source_node_native.hpp"

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "detail/native_promotion.hpp"
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include "detail/producer_surface_bounds.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace chronon3d::graph {

std::optional<raster::BBox> SourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
    const f32 opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
    const bool exclude_from_projection = m_node.shape.type() == ShapeType::FakeBox3D;
    const auto placement = detail::evaluate_source_payload_placement(
        base_matrix,
        opacity,
        ctx,
        m_apply_camera_projection,
        m_defer_camera_projection,
        m_native_3d,
        m_name,
        "predicted_bbox",
        static_cast<std::size_t>(-1),
        exclude_from_projection);
    if (!placement) {
        return std::nullopt;
    }
    const Mat4 matrix = placement->render_matrix;

    f32 spread = 0.0f;
    spread += 8.0f;

    // TICKET-122 FASE 3: GridPlane now goes through 2.5D projection above,
    // so it uses the standard compute_world_bbox path (not native 3D).
    if (m_node.shape.type() == ShapeType::Mesh ||
        (m_apply_camera_projection && ctx.frame_input.has_camera_2_5d &&
         m_node.shape.type() == ShapeType::FakeBox3D)) {
        if (auto bbox = detail::projected_native_3d_bbox(
                ctx, m_node, placement->render_matrix, spread)) {
            return bbox;
        }
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    // Keep the diagnostic/world-space bounds separate from the execution
    // bounds.  Diagnostics may inspect the unclipped geometry, but culling,
    // tile pruning, dirty clipping, and cache state must always consume the
    // same canvas-clipped bbox regardless of the logging flag.
    const auto diagnostic_bbox =
        renderer::compute_world_bbox(m_node.shape, matrix, spread);
    const auto execution = detail::resolve_execution_bbox(
        *placement, diagnostic_bbox, ctx);
    if (!execution) {
        return raster::BBox{0, 0, 0, 0};
    }
    const auto execution_bbox = *execution;

    if (ctx.policy.diagnostics_enabled) {
        spdlog::debug(
            "[source-bbox] node='{}' diagnostic=[{},{},{},{}] execution=[{},{},{},{}]",
            m_name,
            diagnostic_bbox.x0, diagnostic_bbox.y0,
            diagnostic_bbox.x1, diagnostic_bbox.y1,
            execution_bbox.x0, execution_bbox.y0,
            execution_bbox.x1, execution_bbox.y1);
    }

    if (execution_bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};
    }
    return execution_bbox;
}

std::optional<raster::BBox> detail::preflight_diagnostic_bbox(
    const SourceNode& node,
    const RenderGraphContext& ctx) {
    const Mat4 base_matrix = node.m_matrix_override.value_or(node.m_node.world_transform.to_mat4());
    const f32 opacity = node.m_opacity_override.value_or(node.m_node.world_transform.opacity);
    const bool exclude_from_projection = node.m_node.shape.type() == ShapeType::FakeBox3D;
    const auto placement = detail::evaluate_source_payload_placement(
        base_matrix, opacity, ctx, node.m_apply_camera_projection,
        node.m_defer_camera_projection, node.m_native_3d, node.m_name, "diagnostic_bbox",
        static_cast<std::size_t>(-1), exclude_from_projection);
    if (!placement) {
        return std::nullopt;
    }

    const f32 spread = 8.0f;
    if (node.m_node.shape.type() == ShapeType::Mesh ||
        (node.m_apply_camera_projection && ctx.frame_input.has_camera_2_5d &&
         node.m_node.shape.type() == ShapeType::FakeBox3D)) {
        return detail::projected_native_3d_bbox(
            ctx, node.m_node, placement->render_matrix, spread);
    }
    return renderer::compute_world_bbox(
        node.m_node.shape, placement->render_matrix, spread);
}

bool detail::covers_full_frame_as_rectangle(
    const Mat4& matrix,
    f32 width,
    f32 height,
    bool centered
) {
    const f32 x0 = centered ? -width * 0.5f : 0.0f;
    const f32 x1 = centered ?  width * 0.5f : width;
    const f32 y0 = centered ? -height * 0.5f : 0.0f;
    const f32 y1 = centered ?  height * 0.5f : height;

    const Vec4 corners[4] = {
        matrix * Vec4(x0, y0, 0.0f, 1.0f),
        matrix * Vec4(x1, y0, 0.0f, 1.0f),
        matrix * Vec4(x1, y1, 0.0f, 1.0f),
        matrix * Vec4(x0, y1, 0.0f, 1.0f)
    };

    std::array<f32, 4> xs{};
    std::array<f32, 4> ys{};
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();

    for (std::size_t i = 0; i < 4; ++i) {
        const auto& c = corners[i];
        if (std::abs(c.w) < 1e-6f) {
            return false;
        }

        xs[i] = c.x / c.w;
        ys[i] = c.y / c.w;
        min_x = std::min(min_x, xs[i]);
        min_y = std::min(min_y, ys[i]);
        max_x = std::max(max_x, xs[i]);
        max_y = std::max(max_y, ys[i]);
    }

    auto distinct_count = [](const std::array<f32, 4>& values) {
        std::array<f32, 4> unique{};
        std::size_t count = 0;
        for (f32 value : values) {
            bool seen = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (detail::nearly_equal(value, unique[i])) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                unique[count++] = value;
            }
        }
        return count;
    };

    if (distinct_count(xs) != 2 || distinct_count(ys) != 2) {
        return false;
    }

    return detail::nearly_equal(min_x, 0.0f) &&
           detail::nearly_equal(min_y, 0.0f) &&
           detail::nearly_equal(max_x, width) &&
           detail::nearly_equal(max_y, height);
}

cache::NodeCacheKey SourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // TICKET-122: use the current evaluation frame instead of the
    // build-time frame (always Frame{0} for frame-variant nodes),
    // so the cache key differentiates between frames even when
    // params_hash alone would collide (e.g. zoom-identical states).
    key.frame = cache_frame_for_policy(cache_policy(), ctx.frame_input.frame);
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.policy.modular_coordinates));
    if (m_matrix_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
    }
    if (m_opacity_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }
    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B):
    // canonical conditional fold — single implementation for the invariant.
    cache::fold_camera_if(
        key, ctx.frame_input.has_camera_2_5d, ctx.frame_input.camera_2_5d);
    return key;
}

} // namespace chronon3d::graph
