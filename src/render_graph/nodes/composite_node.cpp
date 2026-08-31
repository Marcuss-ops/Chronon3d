// ============================================================================
// composite_node.cpp — CompositeNode::execute() implementation.
//
// Extracted from composite_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "native_surface.hpp"

namespace chronon3d::graph {

namespace {

// ── try_native_affine_composite ──────────────────────────────────────────
//
// Composite `source` onto `destination` when the two surfaces differ in
// size or origin.  Uses `transform_surface_affine` — the direct GPU path
// that handles arbitrary affine transforms without intermediate surfaces.
// Returns true when the native composite succeeded; false when the caller
// should fall back to the CPU path.
bool try_native_affine_composite(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source, const std::optional<raster::BBox>& clip) {
    if (!ctx.services.backend || !ctx.services.surface_registry ||
        (source.width() == destination.width() &&
         source.height() == destination.height())) {
        return false;
    }
    const auto original_destination = destination.surface_handle();
    if (!ensure_native_surface(ctx, destination)) return false;

    OwnedFB source_copy;
    runtime::RenderSurfaceHandle source_handle = source.surface_handle();
    if (source_handle == runtime::kInvalidRenderSurfaceHandle) {
        source_copy = ctx.acquire_owned_fb(source);
        if (!ensure_native_surface(ctx, *source_copy)) {
            if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
                release_native_surface(ctx, destination);
            }
            return false;
        }
        source_handle = source_copy->surface_handle();
    }

    const float dx = static_cast<float>(source.origin_x() - destination.origin_x());
    const float dy = static_cast<float>(source.origin_y() - destination.origin_y());
    const float src_w = static_cast<float>(source.width());
    const float src_h = static_cast<float>(source.height());

    runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f;
    transform.source_x[2] = -dx;
    transform.source_y[1] = 1.0f;
    transform.source_y[2] = -dy;
    transform.max_x = src_w;
    transform.max_y = src_h;
    transform.opacity = 1.0f;
    transform.bilinear = 0u; // Direct 1:1 pixel sampling
    transform.destination_origin_x = destination.origin_x();
    transform.destination_origin_y = destination.origin_y();

    std::int32_t eff_x0 = static_cast<std::int32_t>(dx);
    std::int32_t eff_y0 = static_cast<std::int32_t>(dy);
    std::int32_t eff_x1 = static_cast<std::int32_t>(dx + src_w);
    std::int32_t eff_y1 = static_cast<std::int32_t>(dy + src_h);

    if (clip) {
        const std::int32_t local_clip_x0 = clip->x0 - destination.origin_x();
        const std::int32_t local_clip_y0 = clip->y0 - destination.origin_y();
        const std::int32_t local_clip_x1 = clip->x1 - destination.origin_x();
        const std::int32_t local_clip_y1 = clip->y1 - destination.origin_y();

        eff_x0 = std::max(eff_x0, local_clip_x0);
        eff_y0 = std::max(eff_y0, local_clip_y0);
        eff_x1 = std::min(eff_x1, local_clip_x1);
        eff_y1 = std::min(eff_y1, local_clip_y1);
    }

    if (eff_x1 <= eff_x0 || eff_y1 <= eff_y0) {
        if (source_copy) release_native_surface(ctx, *source_copy);
        return true;
    }

    transform.clip_enabled = 1u;
    transform.clip_rect[0] = eff_x0 + destination.origin_x();
    transform.clip_rect[1] = eff_y0 + destination.origin_y();
    transform.clip_rect[2] = eff_x1 + destination.origin_x();
    transform.clip_rect[3] = eff_y1 + destination.origin_y();

    const auto composited = ctx.services.backend->transform_surface_affine(
        destination.surface_handle(), source_handle, transform);
    if (source_copy) release_native_surface(ctx, *source_copy);
    if (!composited.ok()) {
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        return false;
    }
    return true;
}

bool try_native_composite(RenderGraphContext& ctx, Framebuffer& destination,
                          Framebuffer& source, BlendMode mode,
                          CompositeOperator op,
                          const std::optional<raster::BBox>& clip) {
    // Vulkan's surface compositor supports both the ordinary source-over
    // path and additive overlays. Keeping Add on the CPU fallback would call
    // composite_layer(), whose legacy contract intentionally rejects it.
    if ((mode != BlendMode::Normal && mode != BlendMode::Add) ||
        op != CompositeOperator::SourceOver) {
        return false;
    }
    const auto original_handle = destination.surface_handle();
    const auto original_source_handle = source.surface_handle();
    if (!ensure_native_surface(ctx, destination) ||
        !ensure_native_surface(ctx, source)) {
        if (original_handle == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        if (original_source_handle == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, source);
        }
        return false;
    }
    const auto result = ctx.services.backend->composite_surfaces(
        destination.surface_handle(), source.surface_handle(), mode, op, clip);
    if (result.ok()) return true;
    if (original_handle == runtime::kInvalidRenderSurfaceHandle) {
        release_native_surface(ctx, destination);
    }
    if (original_source_handle == runtime::kInvalidRenderSurfaceHandle) {
        release_native_surface(ctx, source);
    }
    return false;
}

// seed_native_destination() replaces the destination pixels with a copy from
// the source via transform_surface_affine in replace mode — zero intermediate
// surfaces, no temporary clear.  The `replace=true` path in Vulkan's
// composite kernel writes directly into the destination, bypassing blending.
bool seed_native_destination(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source) {
    if (!ctx.services.backend || !ctx.services.surface_registry ||
        source.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        !ctx.services.backend->is_native_surface_valid(source.surface_handle())) {
        return false;
    }
    const auto original_destination = destination.surface_handle();
    if (!ensure_empty_native_surface(ctx, destination)) return false;

    // Use the direct affine path: identity transform copies source to
    // destination pixel-for-pixel without a GPU clear pass.
    runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f;
    transform.source_y[1] = 1.0f;
    transform.max_x = static_cast<float>(source.width());
    transform.max_y = static_cast<float>(source.height());
    transform.opacity = 1.0f;

    const auto seeded = ctx.services.backend->transform_surface_affine(
        destination.surface_handle(), source.surface_handle(), transform);
    if (!seeded.ok()) {
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        return false;
    }
    return true;
}

} // namespace

NodeExecResult CompositeNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    // An upstream node can fail before this level is scheduled.  The
    // executor still drains dependent nodes so the shared frame error can be
    // observed consistently; never dereference a missing input while doing
    // so.  Returning the latched upstream error preserves the original
    // failure at the frame boundary instead of turning it into SIGSEGV.
    if (inputs.size() >= 2 && (!inputs[0] || !inputs[1])) {
        if (ctx.frame_error && ctx.frame_error->has_value()) {
            return NodeExecResult{ctx.frame_error->value()};
        }
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            "Composite",
            "composite input framebuffer is missing"}};
    }

    if (inputs.size() < 2) {
        // A pass-through composite is still a depth producer.  Some graph
        // shapes collapse the bottom input, but the remaining input retains
        // the layer surface represented by m_world_z.  Do not let this
        // optimization bypass DOF provenance publication.
        if (inputs.size() == 1 && inputs[0] &&
            ctx.policy.track_dof_depth && !ctx.node_exec.dof_depth_buffer().empty()) {
            auto& dof_depth = ctx.node_exec.dof_depth_buffer();
            const i32 w = ctx.frame_input.width;
            const auto& camera_2_5d = ctx.frame_input.camera_2_5d;
            const float source_radius = camera_2_5d.dof.enabled
                ? compute_dof_blur_radius(camera_2_5d.dof, camera_2_5d.lens,
                                          m_world_z, static_cast<float>(w))
                : 0.0f;
            const i32 bx0 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x0 : 0;
            const i32 by0 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y0 : 0;
            const i32 bx1 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x1 : w;
            const i32 by1 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y1 : ctx.frame_input.height;
            const Framebuffer& surface = *inputs[0];
            for (i32 y = by0; y < by1; ++y) {
                const i32 sy = y - surface.origin_y();
                if (sy < 0 || sy >= surface.height()) continue;
                const Color* row = surface.pixels_row(sy);
                for (i32 x = bx0; x < bx1; ++x) {
                    const i32 sx = x - surface.origin_x();
                    if (sx < 0 || sx >= surface.width() || row[sx].a <= 0.01f) continue;
                    dof_depth[static_cast<size_t>(y) * w + x] = m_world_z;
                    if (std::isfinite(source_radius) && source_radius >= 0.5f) {
                        auto& coverage = ctx.node_exec.dof_sources();
                        if (!coverage.source_bbox) {
                            coverage.source_bbox = raster::BBox{x, y, x + 1, y + 1};
                        } else {
                            auto& bbox = *coverage.source_bbox;
                            bbox.x0 = std::min(bbox.x0, x);
                            bbox.y0 = std::min(bbox.y0, y);
                            bbox.x1 = std::max(bbox.x1, x + 1);
                            bbox.y1 = std::max(bbox.y1, y + 1);
                        }
                        coverage.max_radius = std::max(coverage.max_radius, source_radius);
                    }
                }
            }
        }
        auto fallback = inputs.empty() ? ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height) : ctx.acquire_owned_fb(*inputs[0]);
        return NodeExecResult{std::move(fallback)};
    }

    const FramebufferRef& bottom = inputs[0];
    const FramebufferRef& top = inputs[1];

    if (ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[dirty-debug] frame={} Composite mode={} bottom_opaque={} top_opaque={} clip=[{}:{} -> {}:{}]",
            static_cast<int>(ctx.frame_input.frame),
            static_cast<int>(m_mode),
            bottom ? (bottom->is_opaque() ? 1 : 0) : 0,
            top ? (top->is_opaque() ? 1 : 0) : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x0 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y0 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x1 : ctx.frame_input.width,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y1 : ctx.frame_input.height
        );
    }

    // Skip-opaque optimization — only applies to SourceOver + Normal blend
    if (m_operator == CompositeOperator::SourceOver &&
        m_mode == BlendMode::Normal && top->is_opaque() &&
        input_bboxes.size() >= 2 && input_bboxes[1].has_value())
    {
        const auto& tb = *input_bboxes[1];
        if (tb.x0 <= 0 && tb.y0 <= 0 && tb.x1 >= ctx.frame_input.width && tb.y1 >= ctx.frame_input.height) {
            if (!ctx.node_exec.clip_rect ||
                (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                 ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height))
            {
                if (ctx.node_exec.counters) {
                    ctx.node_exec.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                    const uint64_t area = static_cast<uint64_t>(ctx.frame_input.width) * static_cast<uint64_t>(ctx.frame_input.height);
                    ctx.node_exec.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
                    // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — skip-opaque
                    // optimization is a full-frame pass.  The top input may
                    // be a cached framebuffer, so it must remain immutable;
                    // do not swap its pixel storage into the result.
                    ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
                }
                auto result = ctx.acquire_owned_fb(top->width(), top->height(), false);
                for (i32 y = 0; y < top->height(); ++y) {
                    std::memcpy(result->pixels_row(y), top->pixels_row(y),
                                static_cast<std::size_t>(top->width()) * sizeof(Color));
                }
                result->set_origin(top->origin_x(), top->origin_y());
                result->set_opaque(true);
                return NodeExecResult{std::move(result)};
            }
        }
    }

    // ── Acquire output framebuffer ────────────────────────────────────
    //
    // The existing acquire_owned_fb(const Framebuffer&) overload already
    // implements zero-copy via scratch.reusable_inputs when the input has
    // sole ownership (consumer_remaining==1 && use_count==1).  When the
    // bottom is reusable, it swaps pixel storage without copying ~8MB.
    //
    // NOTE: in a chain of composites the CachedFB often has use_count>1
    // because the node_cache holds a reference, so the reusable path is
    // skipped and a full copy occurs.  This is mitigated by increasing
    // the framebuffer pool budget (1 GB) and max_buffers_per_size_class
    // (8) to reduce pool thrashing + eviction pressure — the root cause
    // of the 70.9K ms compositenode_acquire_wall_ms bottleneck.
    const auto t_acquire0 = profiling::now();
    const bool bottom_matches_canvas =
        bottom->width() == ctx.frame_input.width &&
        bottom->height() == ctx.frame_input.height;
    OwnedFB result;
    if (bottom_matches_canvas) {
        result = ctx.acquire_owned_fb(*bottom);
        const bool result_native_valid =
            result->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
            ctx.services.backend &&
            ctx.services.backend->is_native_surface_valid(result->surface_handle());
        const bool bottom_native_valid =
            bottom->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
            ctx.services.backend &&
            ctx.services.backend->is_native_surface_valid(bottom->surface_handle());
        if (result->surface_handle() != bottom->surface_handle() && bottom_native_valid) {
            if (!seed_native_destination(ctx, *result, *bottom)) {
                if (ctx.policy.require_native_gpu) {
                    return NodeExecutionError{
                        RenderBackendErrorCode::ExecutionFailure,
                        "CompositeNode",
                        "native residency violation: failed to seed destination from bottom surface"};
                }
            }
        }
    } else {
        result = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, true);
        if (ctx.services.backend) {
            if (!try_native_affine_composite(ctx, *result, *bottom, std::nullopt)) {
                if (ctx.services.surface_registry && ctx.services.backend->supports_native_surfaces()) {
                    const bool destination_ready = ensure_native_surface(ctx, *result);
                    auto& mutable_bottom = const_cast<Framebuffer&>(*bottom);
                    const bool source_ready = ensure_native_surface(ctx, mutable_bottom);
                    if (ctx.policy.require_native_gpu &&
                        (!destination_ready || !source_ready)) {
                        return NodeExecutionError{
                            RenderBackendErrorCode::ExecutionFailure,
                            "CompositeNode",
                            "native residency violation while materializing dimension-mismatched inputs"};
                    }
                }
                ctx.services.backend->composite_layer(*result, *bottom, BlendMode::Normal);
            }
        }
        // F3.2 — size mismatch forces a full-canvas composite_layer
        // (every pixel touched). Surface as a full-frame pass. The byte
        // side-cost is captured separately by framebuffer_copy_wall_ms when the
        // pool returns a re-used allocation that demands std::copy.
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const auto t_acquire1 = profiling::now();
    if (ctx.node_exec.counters) {
        const auto acquire_ms = static_cast<uint64_t>(
            profiling::duration_ms(t_acquire0, t_acquire1));
        ctx.node_exec.counters->compositenode_acquire_wall_ms.fetch_add(acquire_ms, std::memory_order_relaxed);
    }
    // Note: when bottom doesn't fit (rare), the bottom composite_layer
    // call is included in this timing AND already tracked in
    // compositenode_blend_wall_ms.  This minor double-counting is acceptable
    // because the common path (bottom fits, >=99% of frames) is pure.

    // Start dispatch timing here — AFTER acquire and optional bottom composite,
    // so dispatch_ms measures only clip/bbox computation + overhead, without
    // double-counting bottom composite time (already in compositenode_blend_wall_ms).
    const auto t_dispatch0 = profiling::now();

    if (ctx.services.backend) {
        std::optional<raster::BBox> clip = (input_bboxes.size() >= 2) ? input_bboxes[1] : std::nullopt;
        if (ctx.node_exec.clip_rect) {
            if (clip) {
                clip = raster::BBox{
                    .x0 = std::max(clip->x0, ctx.node_exec.clip_rect->x0),
                    .y0 = std::max(clip->y0, ctx.node_exec.clip_rect->y0),
                    .x1 = std::min(clip->x1, ctx.node_exec.clip_rect->x1),
                    .y1 = std::min(clip->y1, ctx.node_exec.clip_rect->y1)
                };
                if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                    clip = raster::BBox{0, 0, 0, 0};
                }
            } else {
                clip = ctx.node_exec.clip_rect;
            }
        }

        // Declare these in the backend scope so the aggregate non-blend
        // calculation below can reference them regardless of whether the
        // inner telemetry blocks are entered.
        // Use auto (same as rest of codebase) — profiling::now() returns
        // a POD timestamp, default-constructed without clock syscall.
        auto _de = profiling::now(); // dispatch end
        auto _os = profiling::now(); // overhead start
        auto _oe = profiling::now(); // overhead end

        // Record dispatch time before composite_layer call.
        if (ctx.node_exec.counters) {
            _de = profiling::now();
            const auto dms = static_cast<uint64_t>(profiling::duration_ms(t_dispatch0, _de));
            ctx.node_exec.counters->compositenode_dispatch_wall_ms.fetch_add(dms, std::memory_order_relaxed);
        }

        // Check if stencil/silhouette operator — these use the underlying
        // composite operator field instead of a blend mode when applicable.
        const bool native_composite = try_native_composite(
            ctx, *result, *top, m_mode, m_operator, clip);
        const bool native_dimension_composite = !native_composite &&
            m_mode == BlendMode::Normal &&
            m_operator == CompositeOperator::SourceOver &&
            try_native_affine_composite(ctx, *result, *top, clip);
        if (ctx.policy.require_native_gpu &&
            !native_composite && !native_dimension_composite) {
            spdlog::error(
                "[native-residency] CompositeNode cannot keep frame {} on GPU "
                "(destination={} source={})",
                static_cast<int>(ctx.frame_input.frame),
                result->surface_handle(), top->surface_handle());
            return NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                "CompositeNode",
                "native residency violation: composite inputs could not be materialized"};
        }
        if (native_composite || native_dimension_composite) {
            // The native path has already composed the logical surfaces. The
            // CPU pixels remain a reference snapshot for any legacy consumer;
            // the handle is the authoritative value for subsequent native
            // composite nodes.
        } else if (m_operator != CompositeOperator::SourceOver) {
            // Stencil/Silhouette: use Normal blend to copy top first, then
            // apply the operator via the backend (which handles the masking).
            // The operator is passed along so the backend can apply the
            // appropriate matte-style coverage to the backdrop.
            if (ctx.services.backend && ctx.services.surface_registry && ctx.services.backend->supports_native_surfaces()) {
                const bool destination_ready = ensure_native_surface(ctx, *result);
                auto& mutable_top = const_cast<Framebuffer&>(*top);
                const bool source_ready = ensure_native_surface(ctx, mutable_top);
                if (ctx.policy.require_native_gpu &&
                    (!destination_ready || !source_ready)) {
                    return NodeExecutionError{
                        RenderBackendErrorCode::ExecutionFailure,
                        "CompositeNode",
                        "native residency violation in non-SourceOver composite"};
                }
            }
            ctx.services.backend->composite_layer(*result, *top, m_mode, clip, m_operator);
    } else {
        // VulkanBackend::composite_layer is a native primitive even when the
        // fast-path probe above declined the optimized SourceOver route
        // (for example Add/stencil operators).  Materialize both handles
        // before dispatching that fallback; otherwise the backend receives
        // the framebuffer sentinel 0 and fails later in resolve_image().
        if (ctx.services.backend && ctx.services.surface_registry && ctx.services.backend->supports_native_surfaces()) {
            const bool destination_ready = ensure_native_surface(ctx, *result);
            auto& mutable_top = const_cast<Framebuffer&>(*top);
            const bool source_ready = ensure_native_surface(ctx, mutable_top);
            if (!destination_ready || !source_ready) {
                spdlog::error("[composite] native fallback could not materialize surfaces "
                              "destination={} source={}",
                              result->surface_handle(), mutable_top.surface_handle());
                if (ctx.policy.require_native_gpu) {
                    return NodeExecutionError{
                        RenderBackendErrorCode::ExecutionFailure,
                        "CompositeNode",
                        "native residency violation in composite fallback"};
                }
            }
        }
        ctx.services.backend->composite_layer(*result, *top, m_mode, clip);
    }

        // ── Post-blend overhead ────────────────────────────────────────
        _os = profiling::now();

        auto& dof_depth = ctx.node_exec.dof_depth_buffer();
        if (ctx.policy.track_dof_depth && !dof_depth.empty()) {
            const i32 w = ctx.frame_input.width;
            const float wz = m_world_z;
            const auto& camera_2_5d = ctx.frame_input.camera_2_5d;
            const float source_radius = camera_2_5d.dof.enabled
                ? compute_dof_blur_radius(camera_2_5d.dof, camera_2_5d.lens,
                                          wz, static_cast<float>(w))
                : 0.0f;
            const i32 bx0 = clip ? clip->x0 : 0;
            const i32 by0 = clip ? clip->y0 : 0;
            const i32 bx1 = clip ? clip->x1 : ctx.frame_input.width;
            const i32 by1 = clip ? clip->y1 : ctx.frame_input.height;
            for (i32 y = by0; y < by1; ++y) {
                const i32 sy = y - top->origin_y();
                if (sy < 0 || sy >= top->height()) continue;
                const Color* src_row = top->pixels_row(sy);
                for (i32 x = bx0; x < bx1; ++x) {
                    const i32 sx = x - top->origin_x();
                    if (sx < 0 || sx >= top->width()) continue;
                    if (src_row[sx].a > 0.01f) {
                        dof_depth[static_cast<size_t>(y) * w + x] = wz;
                        if (std::isfinite(source_radius) && source_radius >= 0.5f) {
                            auto& coverage = ctx.node_exec.dof_sources();
                            if (!coverage.source_bbox) {
                                coverage.source_bbox = raster::BBox{x, y, x + 1, y + 1};
                            } else {
                                auto& bbox = *coverage.source_bbox;
                                bbox.x0 = std::min(bbox.x0, x);
                                bbox.y0 = std::min(bbox.y0, y);
                                bbox.x1 = std::max(bbox.x1, x + 1);
                                bbox.y1 = std::max(bbox.y1, y + 1);
                            }
                            coverage.max_radius = std::max(coverage.max_radius,
                                                            source_radius);
                        }
                    }
                }
            }
        }

        if (m_mode == BlendMode::Normal && top->is_opaque() &&
            input_bboxes.size() >= 2 && input_bboxes[1].has_value() &&
            input_bboxes[1]->x0 <= 0 && input_bboxes[1]->y0 <= 0 &&
            input_bboxes[1]->x1 >= ctx.frame_input.width && input_bboxes[1]->y1 >= ctx.frame_input.height &&
            (!ctx.node_exec.clip_rect ||
             (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
              ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height)))
        {
            result->set_opaque(true);
        }

        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = clip ? (static_cast<uint64_t>(std::max(0, clip->x1 - clip->x0)) * std::max(0, clip->y1 - clip->y0))
                                 : static_cast<uint64_t>(ctx.frame_input.width * ctx.frame_input.height);
            if (m_mode == BlendMode::Normal && top->is_opaque()) {
                ctx.node_exec.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
            } else {
                ctx.node_exec.counters->composite_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }

        if (ctx.node_exec.counters) {
            _oe = profiling::now();
            const auto oms = static_cast<uint64_t>(
                profiling::duration_ms(_os, _oe));
            ctx.node_exec.counters->compositenode_overhead_wall_ms.fetch_add(oms, std::memory_order_relaxed);
        }

        // Aggregate non-blend cost: (acquire + dispatch) + overhead.
        // Blend (composite_layer) is excluded because it runs between _de and _os.
        if (ctx.node_exec.counters) {
            const auto nb_work = profiling::duration_us(t_acquire0, _de)
                               + profiling::duration_us(_os, _oe);
            const auto nb_us = static_cast<uint64_t>(std::max(0.0, nb_work));
            ctx.node_exec.counters->compositenode_internal_wall_us.fetch_add(
                nb_us, std::memory_order_relaxed);
        }
    }
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
