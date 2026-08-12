#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <limits>

namespace chronon3d::graph {

namespace {

constexpr f32 kSeedFrameEpsilon = 1e-3f;

[[nodiscard]] bool nearly_equal(f32 a, f32 b, f32 eps = kSeedFrameEpsilon) {
    return std::abs(a - b) <= eps;
}

[[nodiscard]] bool covers_full_frame_as_rectangle(
    const Mat4& matrix,
    f32 width,
    f32 height,
    bool centered = false
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
                if (nearly_equal(value, unique[i])) {
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

    return nearly_equal(min_x, 0.0f) &&
           nearly_equal(min_y, 0.0f) &&
           nearly_equal(max_x, width) &&
           nearly_equal(max_y, height);
}

} // namespace

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
    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B).
    // Conditional on `has_camera_2_5d` globally — AE_CAM_02 zoom-only
    // frames now produce distinct per-frame keys.
    if (ctx.frame_input.has_camera_2_5d) {
        cache::fold_camera_into_params_hash(key, ctx.frame_input.camera_2_5d);
    }
    return key;
}

NodeExecResult SourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("source_render", trace_category::kRasterize);
    const bool full_frame_seed = can_seed_full_frame(ctx);
    const Mat4 base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
    const f32 opacity = m_opacity_override.value_or(m_node.world_transform.opacity);

    // A screen-space solid background is semantically a clear, not a shape
    // raster.  Keep this predicate deliberately narrow: only an un-stroked,
    // solid, opaque rectangle with an affine placement that covers exactly
    // the logical canvas may take it.  Gradients, rounded corners, camera
    // projection, clips and transformed rectangles continue through the
    // general raster path.
    bool direct_full_frame_fill = false;
    if (!m_apply_camera_projection && !m_defer_camera_projection && !m_native_3d &&
        m_node.shape.type() == ShapeType::Rect &&
        !m_node.shape.rect().stroke.enabled &&
        m_node.fill.type == FillType::Solid &&
        m_node.corner_radius <= 0.0f && opacity >= 0.999f &&
        m_node.color.a >= 0.999f &&
        (!ctx.node_exec.clip_rect ||
         (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
          ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width &&
          ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height))) {
        const auto placement = detail::evaluate_source_payload_placement(
            base_matrix,
            opacity,
            ctx,
            false,
            false,
            false,
            m_name,
            "direct_full_frame_fill");
        direct_full_frame_fill = placement && covers_full_frame_as_rectangle(
            placement->render_matrix,
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height),
            false);
    }

    // Skip clear when full-frame opaque with integer translation — no
    // sub-pixel gaps are possible because the source covers every pixel
    // and the composite path uses integer-rounded coordinates.
    bool skip_clear = false;
    if (full_frame_seed) {
        const auto mat = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        // Identity scale + rotation (coefficients ~1 or ~0) and integer translation
        const bool identity_scale_rot =
            std::abs(mat[0][0] - 1.0f) < 1e-4f && std::abs(mat[0][1]) < 1e-4f &&
            std::abs(mat[1][0]) < 1e-4f && std::abs(mat[1][1] - 1.0f) < 1e-4f;
        if (identity_scale_rot) {
            const float tx = mat[3][0];
            const float ty = mat[3][1];
            skip_clear = std::abs(tx - std::round(tx)) < 1e-4f &&
                         std::abs(ty - std::round(ty)) < 1e-4f;
        }
    }

    auto fb = ctx.acquire_owned_fb(
        ctx.frame_input.width,
        ctx.frame_input.height,
        !skip_clear && !direct_full_frame_fill);

    if (direct_full_frame_fill) {
        Color fill_color = m_node.color.to_linear();
        fill_color.a *= opacity;
        fb->clear(fill_color);
        fb->set_opaque(true);
        if (ctx.policy.diagnostics_enabled) {
            spdlog::info("[source-fast-path] node='{}' direct_full_frame_fill=1 pixels={}",
                         m_name,
                         static_cast<std::uint64_t>(ctx.frame_input.width) *
                             static_cast<std::uint64_t>(ctx.frame_input.height));
        }
        return NodeExecResult{std::move(fb)};
    }

    if (ctx.services.backend) {
        RenderState state;
        state.frame_number = static_cast<int>(ctx.frame_input.frame);
        state.ssaa_factor = ctx.policy.ssaa_factor;

        const auto placement = detail::evaluate_source_payload_placement(
            base_matrix,
            opacity,
            ctx,
            m_apply_camera_projection,
            m_defer_camera_projection,
            m_native_3d,
            m_name,
            "execute",
            static_cast<std::size_t>(-1),
            m_node.shape.type() == ShapeType::FakeBox3D);
        if (!placement) {
            if (ctx.policy.diagnostics_enabled) {
                spdlog::info(
                    "[source-skip] node='{}' proj.visible=false frame={} — returning empty fb",
                    m_name,
                    ctx.frame_input.sample_time.integral_frame());
            }
            fb->set_opaque(false);
            return NodeExecResult{std::move(fb)};
        }
        state.matrix = placement->render_matrix;
        state.opacity = opacity;
        state.shape_processor = ctx.node_exec.current_shape_processor;
        state.processor_snapshot = ctx.node_exec.processor_snapshot;
        state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        state.frame_number = static_cast<int>(ctx.frame_input.frame);

        state.clip_rect = ctx.node_exec.clip_rect;
        state.diagnostics_enabled = ctx.policy.diagnostics_enabled;

        if (ctx.frame_input.has_camera_2_5d) {
            state.projection  = ctx.frame_input.projection_ctx;
        }

        // P0-1: draw_node() returns void — backend failures (e.g. missing
        // processor-context, unsupported shape) are logged but cannot propagate
        // to the executor.  Tracked for Phase C post-freeze.
        ctx.services.backend->draw_node(*fb, m_node, state, ctx.frame_input.camera, ctx.frame_input.width, ctx.frame_input.height);
        // A fully opaque source becomes translucent when the layer opacity
        // is animated below one; keep the metadata truthful so CompositeNode
        // cannot take the opaque fast path and replace the background.
        fb->set_opaque(full_frame_seed && state.opacity >= 1.0f);

        if (ctx.policy.diagnostics_enabled) {
            int nonzero_pixels = 0;
            for (i32 y = 0; y < fb->height(); ++y) {
                const Color* row = fb->pixels_row(y);
                for (i32 x = 0; x < fb->width(); ++x) {
                    const Color& c = row[x];
                    if (c.a > 0.001f || c.r > 0.001f || c.g > 0.001f || c.b > 0.001f) {
                        ++nonzero_pixels;
                    }
                }
            }

            spdlog::info(
                "[source-debug] node='{}' shape={} nonzero_pixels={} opacity={:.3f} matrix_tx={:.3f} matrix_ty={:.3f} det2d={:.6f}",
                m_name,
                static_cast<int>(m_node.shape.type()),
                nonzero_pixels,
                state.opacity,
                state.matrix[3][0],
                state.matrix[3][1],
                glm::determinant(glm::mat3(
                    state.matrix[0][0], state.matrix[0][1], state.matrix[0][3],
                    state.matrix[1][0], state.matrix[1][1], state.matrix[1][3],
                    state.matrix[3][0], state.matrix[3][1], state.matrix[3][3]
                ))
            );
        }
    }
    return NodeExecResult{std::move(fb)};
}

bool SourceNode::can_seed_full_frame(const RenderGraphContext& ctx) const noexcept {
    // Frame-invariant + reusable across frames = eligible to skip the clear.
    // TICKET-ae-cam-hash-collision Soluzione B (rendering-side) — bail
    // when a camera is active even if the per-node flag is false: with
    // a 2.5D camera the screen-space "full frame" assumption no longer
    // holds (zoom changes the effective coverage), so full-frame
    // seeding would produce stale FBs that bypass the cache-key fix.
    // TICKET-TEXT-CLEANUP-8: m_uses_2_5d_projection removed.  2.5D
    // projection is now conditioned on has_camera_2_5d globally.
    if (!m_cache_policy.reusable_across_frames()
        || ctx.frame_input.has_camera_2_5d) {
        return false;
    }

    if (m_node.shape.type() != ShapeType::Image) {
        return false;
    }

    const auto& img = m_node.shape.image();
    const auto& tr = m_node.world_transform;
    const f32 opacity = m_opacity_override.value_or(tr.opacity);

    if (ctx.node_exec.clip_rect) {
        const bool clip_is_full = ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                                  ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height;
        if (!clip_is_full) {
            return false;
        }
    }

    const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.frame_input.width)) < kSeedFrameEpsilon &&
                           std::abs(img.size.y - static_cast<f32>(ctx.frame_input.height)) < kSeedFrameEpsilon;
    const bool opaque = img.opacity >= 0.999f && opacity >= 0.999f;
    if (!full_size || !opaque) {
        return false;
    }

    const Mat4 local_matrix = m_matrix_override.value_or(tr.to_mat4());
    // TICKET-TEXT-CLEANUP-5: centering is now baked into matrix_override
    // by the source pass / refresh.  m_centered removed.
    const auto placement = detail::evaluate_source_payload_placement(
        local_matrix,
        opacity,
        ctx,
        false,
        false,
        false,
        m_name,
        "can_seed_full_frame");
    if (!placement) {
        return false;
    }

    return covers_full_frame_as_rectangle(
        placement->render_matrix,
        static_cast<f32>(ctx.frame_input.width),
        static_cast<f32>(ctx.frame_input.height),
        false);
}

} // namespace chronon3d::graph
