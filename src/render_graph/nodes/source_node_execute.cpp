// ═══════════════════════════════════════════════════════════════════════════
// source_node_execute.cpp — per-frame execution for SourceNode.
//
// Split out of source_node.cpp: SourceNode::execute (frame lifecycle: cached
// result reuse, full-frame-seed / clear decisions, producer surface
// resolution, native fast-path dispatch, backend draw) and
// SourceNode::can_seed_full_frame.
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

NodeExecResult SourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_TRACE_SCOPE("chronon.node", "source_render");
    if (m_cache_policy.reusable_across_frames() && m_cached_result &&
        !ctx.frame_input.has_camera_2_5d &&
        (!ctx.node_exec.clip_rect || (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                                      ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width &&
                                      ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height))) {
        return NodeExecResult{ctx.acquire_owned_fb(*m_cached_result)};
    }
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
        direct_full_frame_fill = placement && detail::covers_full_frame_as_rectangle(
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

    // Only image overlays need a predicted visual box here.  Background and
    // video producers are intentionally full-canvas and must not pay for an
    // extra per-frame bounds calculation.
    std::optional<raster::BBox> source_bbox;
    if (m_node.shape.type() == ShapeType::Image && !full_frame_seed) {
        source_bbox = predicted_bbox(ctx);
    }
    const auto producer_surface = detail::resolve_producer_surface_bounds(
        ctx.frame_input.width,
        ctx.frame_input.height,
        m_node.shape.type() == ShapeType::Image
            ? detail::ProducerSurfaceKind::Image
            : detail::ProducerSurfaceKind::Background,
        source_bbox ? &*source_bbox : nullptr);
    detail::record_producer_surface(
        ctx.node_exec.counters,
        m_node.shape.type() == ShapeType::Image
            ? detail::ProducerSurfaceKind::Image
            : detail::ProducerSurfaceKind::Background,
        producer_surface,
        ctx.frame_input.width,
        ctx.frame_input.height);

    OwnedFB fb;
    if (producer_surface.tight && ctx.services.framebuffer_pool &&
        !ctx.node_exec.planned_physical_slot) {
        fb = ctx.services.framebuffer_pool->acquire_owned_exact(
            producer_surface.width(), producer_surface.height(),
            !skip_clear && !direct_full_frame_fill);
        fb->set_origin(producer_surface.bounds.x0, producer_surface.bounds.y0);
    } else {
        fb = ctx.acquire_owned_fb(
            producer_surface.width(),
            producer_surface.height(),
            !skip_clear && !direct_full_frame_fill,
            producer_surface.bounds);
    }

    // Keep the CPU clear only for the software backend.  A Vulkan video job
    // must materialize even a full-frame color layer as a native surface so
    // the final framebuffer can stay device-local all the way to NVENC.
    const bool native_surface_backend =
        ctx.services.backend && ctx.services.backend->supports_native_video_surface();
    if (direct_full_frame_fill && !native_surface_backend) {
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

        // Formatted per-frame logging is forbidden in the hot loop (P1.0).
        // Counters and stage telemetry in the render receipt remain the
        // always-on observation surface.
        const bool native_filled = detail::try_native_rect_fill(ctx, *fb, m_node, state);
        const bool native_image = !native_filled &&
            m_node.shape.type() == ShapeType::Image &&
            detail::try_native_image(ctx, *fb, m_node, state);
        if (!native_filled && !native_image) {
            // Generic Vulkan producers (circle, rounded rect, line, path)
            // also have native implementations, but unlike the rect/image
            // fast paths they materialize the surface inside draw_node().
            // Ensure the destination exists before dispatch so the backend
            // can select its native shape path instead of seeing a CPU-only
            // framebuffer and reporting a misleading unsupported capability.
            if (ctx.services.backend->supports_native_surfaces() &&
                ctx.services.surface_registry && !ensure_native_surface(ctx, *fb, "SourceNode.image.fallback")) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    m_name,
                    "failed to materialize native source surface"};
            }
            const auto draw_result = ctx.services.backend->draw_node(
                *fb, m_node, state, ctx.frame_input.camera,
                ctx.frame_input.width, ctx.frame_input.height);
            if (!draw_result.ok()) {
                return NodeExecResult{NodeExecutionError{
                    draw_result.error().code,
                    m_name,
                    draw_result.error().message}};
            }
        }
        // A fully opaque source becomes translucent when the layer opacity
        // is animated below one; keep the metadata truthful so CompositeNode
        // cannot take the opaque fast path and replace the background.
        fb->set_opaque(full_frame_seed && state.opacity >= 1.0f);

        // Diagnostics: only log in debug mode without full-buffer pixel scanning
    }
    if (m_cache_policy.reusable_across_frames() && fb &&
        !ctx.frame_input.has_camera_2_5d &&
        fb->surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        // A cached CPU framebuffer must not carry a frame-scoped Vulkan
        // handle into a later cache hit.
        m_cached_result = std::make_shared<Framebuffer>(*fb);
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

    const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.frame_input.width)) < detail::kSeedFrameEpsilon &&
                           std::abs(img.size.y - static_cast<f32>(ctx.frame_input.height)) < detail::kSeedFrameEpsilon;
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

    return detail::covers_full_frame_as_rectangle(
        placement->render_matrix,
        static_cast<f32>(ctx.frame_input.width),
        static_cast<f32>(ctx.frame_input.height),
        false);
}

} // namespace chronon3d::graph
