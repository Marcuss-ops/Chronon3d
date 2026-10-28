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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "native_surface.hpp"
#include "composite_node_detail.hpp"
#include "composite_backdrop_detail.hpp"
// ctx.frame_error (runtime::RenderErrorSlot) is dereferenced below; the
// context header only forward-declares the class.
#include <chronon3d/runtime/frame/frame_error_slot.hpp>

namespace chronon3d::graph {



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
    if (ctx.frame_error && ctx.frame_error->has_error()) {
        if (auto error = ctx.frame_error->load()) {
            return NodeExecResult{std::move(*error)};
        }
    }

    const FramebufferRef empty_fb{};
    const FramebufferRef& in0 = inputs.size() >= 1 ? inputs[0] : empty_fb;
    const FramebufferRef& in1 = inputs.size() >= 2 ? inputs[1] : empty_fb;

    if (!in0 && !in1) {
        if (inputs.empty()) {
            auto fallback = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
            return NodeExecResult{std::move(fallback)};
        }
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            "Composite",
            "composite input framebuffer is missing"}};
    }

    const FramebufferRef& bottom = in0 ? in0 : in1;
    const FramebufferRef& top = in0 ? in1 : empty_fb;

    if (m_backdrop_blur) {
        if (!top || m_operator != CompositeOperator::SourceOver || m_mode != BlendMode::Normal)
            return NodeExecutionError{RenderBackendErrorCode::InvalidInput,
                "CompositeNode", "backdrop blur requires a normal source-over shape layer"};
        if (ctx.backend && ctx.surface_registry && ctx.backend_stats_api()->supports_native_surfaces() &&
            m_backdrop_blur->brightness == 1.0f && m_backdrop_blur->saturation == 1.0f &&
            m_backdrop_blur->rim_light == 0.0f && m_backdrop_blur->refraction == 0.0f &&
            m_backdrop_blur->surface_noise == 0.0f &&
            bottom->width() == ctx.frame_input.width && bottom->height() == ctx.frame_input.height &&
            top->width() == bottom->width() && top->height() == bottom->height() &&
            bottom->origin_x() == 0 && bottom->origin_y() == 0 &&
            top->origin_x() == 0 && top->origin_y() == 0) {
            auto result = ctx.acquire_owned_fb(*bottom);
            auto horizontal = ctx.acquire_owned_fb(bottom->width(), bottom->height(), false);
            auto blurred = ctx.acquire_owned_fb(bottom->width(), bottom->height(), false);
            auto masked = ctx.acquire_owned_fb(bottom->width(), bottom->height(), false);
            if (!ensure_native_surface(ctx, *result, "CompositeNode.backdrop.destination") ||
                !ensure_native_surface(ctx, *horizontal, "CompositeNode.backdrop.horizontal") ||
                !ensure_native_surface(ctx, *blurred, "CompositeNode.backdrop.blurred") ||
                !ensure_native_surface(ctx, *masked, "CompositeNode.backdrop.masked") ||
                !ensure_native_surface(ctx, const_cast<Framebuffer&>(*top), "CompositeNode.backdrop.mask"))
                return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode", "failed to materialize backdrop blur surfaces"};
            const auto h = ctx.backend_surface_api()->blur_surface(horizontal->surface_handle(),
                bottom->surface_handle(), m_backdrop_blur->blur, true);
            const auto v = h.ok() ? ctx.backend_surface_api()->blur_surface(blurred->surface_handle(),
                horizontal->surface_handle(), m_backdrop_blur->blur, false) : h;
            const auto m = v.ok() ? ctx.backend_surface_api()->matte_surface(masked->surface_handle(),
                blurred->surface_handle(), top->surface_handle(), false, false) : v;
            const auto c = m.ok() ? ctx.backend_surface_api()->composite_surfaces(result->surface_handle(),
                masked->surface_handle(), BlendMode::Normal, CompositeOperator::SourceOver, std::nullopt) : m;
            const auto overlay = c.ok() ? ctx.backend_surface_api()->composite_surfaces(result->surface_handle(),
                top->surface_handle(), BlendMode::Normal, CompositeOperator::SourceOver, std::nullopt) : c;
            if (!overlay.ok())
                return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode", "native backdrop blur operation failed"};
            result->mark_gpu_authoritative();
            return NodeExecResult{std::move(result)};
        }
        return detail::execute_backdrop_software(ctx, *bottom, *top, *m_backdrop_blur, m_mode);
    }

    // Which inputs the compiler actually wired here. A Composite with a single
    // input is a pass-through (the `!top` early return below), so this line is
    // what separates "the layer was never wired" from "the layer was wired and
    // the blend dropped it" — the two failures look identical in the output.
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info(
            "[composite-inputs] frame={} inputs={} in0={} in1={} "
            "bottom={}x{} top={}x{} mode={}",
            static_cast<int>(ctx.frame_input.frame), inputs.size(),
            in0 ? 1 : 0, in1 ? 1 : 0,
            bottom ? bottom->width() : -1, bottom ? bottom->height() : -1,
            top ? top->width() : -1, top ? top->height() : -1,
            static_cast<int>(m_mode));
    }


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
    if (!ctx.policy.require_native_gpu &&
        top && m_operator == CompositeOperator::SourceOver &&
        m_mode == BlendMode::Normal && top->is_opaque() &&
        top->is_cpu_authoritative() &&
        top->data() != nullptr &&
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
        const bool native_backend =
            ctx.backend && ctx.surface_registry &&
            ctx.backend_stats_api()->supports_native_surfaces();
        auto& mutable_bottom = const_cast<Framebuffer&>(*bottom);
        bool bottom_native_valid =
            bottom->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
            native_backend &&
            ctx.backend_surface_api()->is_native_surface_valid(bottom->surface_handle());
        if (native_backend && !bottom_native_valid) {
            if (!ensure_native_surface(ctx, mutable_bottom, "CompositeNode.bottom")) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode",
                    "failed to materialize native bottom surface"};
            }
            bottom_native_valid = true;
        }
        result = ctx.acquire_owned_fb(*bottom);
        if (native_backend && result->surface_handle() != bottom->surface_handle()) {
            if (!detail::seed_native_destination(ctx, *result, *bottom)) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode",
                    "failed to seed native destination from bottom surface"};
            }
        }
    } else {
        result = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, true);
        if (ctx.backend) {
            if (!detail::try_native_affine_composite(ctx, *result, *bottom, std::nullopt)) {
                // try_native_affine_composite returns false both when the
                // native affine path is unavailable (software backend, or
                // source/destination already same size) and when it failed.
                // Materialize native surfaces when the backend owns them;
                // then fall back to the CPU composite_layer.  Only a strict
                // require_native_gpu policy turns materialization failure
                // into a hard error.  (Regression from the Sep-5 TU split
                // 39a5c03c9: it deleted this CPU fallback, making every
                // software dimension-mismatch composite — e.g. shadow
                // receivers smaller than the canvas — fail hard.)
                if (ctx.surface_registry &&
                    ctx.backend_stats_api()->supports_native_surfaces()) {
                    const bool destination_ready = ensure_native_surface(
                        ctx, *result, "CompositeNode.dimension.destination");
                    auto& mutable_bottom = const_cast<Framebuffer&>(*bottom);
                    const bool source_ready = ensure_native_surface(
                        ctx, mutable_bottom, "CompositeNode.dimension.source");
                    if (ctx.policy.require_native_gpu &&
                        (!destination_ready || !source_ready)) {
                        return NodeExecutionError{
                            RenderBackendErrorCode::ExecutionFailure,
                            "CompositeNode",
                            "native residency violation while materializing "
                            "dimension-mismatched inputs"};
                    }
                }
                ctx.backend_frame_api()->composite_layer(
                    *result, *bottom, BlendMode::Normal);
            }
        }
        // F3.2 — size mismatch forces a full-canvas composite (every pixel
        // touched). Surface as a full-frame pass. The byte side-cost is
        // captured separately by framebuffer_copy_wall_ms when the pool
        // returns a re-used allocation that demands std::copy.
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
    if (!top) {
        if (ctx.policy.track_dof_depth) {
            const float depth = m_has_world_position
                ? camera_space_depth(ctx.frame_input.camera_2_5d, m_world_position)
                : camera_space_depth(ctx.frame_input.camera_2_5d,
                                     Vec3{0.0f, 0.0f, m_world_z});
            detail::publish_dof_provenance(
                ctx, *result, m_unique_id, depth,
                detail::dof_source_radius(ctx, depth),
                ctx.node_exec.clip_rect,
                ctx.frame_input.width, ctx.frame_input.height);
        }
        return NodeExecResult{std::move(result)};
    }

    const auto t_dispatch0 = profiling::now();

    if (ctx.backend) {
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
        auto _oe = _os;              // overhead end

        // Record dispatch time before composite_layer call.
        if (ctx.node_exec.counters) {
            _de = profiling::now();
            const auto dms = static_cast<uint64_t>(profiling::duration_ms(t_dispatch0, _de));
            ctx.node_exec.counters->compositenode_dispatch_wall_ms.fetch_add(dms, std::memory_order_relaxed);
        }

        // Check if stencil/silhouette operator — these use the underlying
        // composite operator field instead of a blend mode when applicable.
        const bool native_composite = detail::try_native_composite(
            ctx, *result, *top, m_mode, m_operator, clip);
        const bool native_dimension_composite = !native_composite &&
            m_operator == CompositeOperator::SourceOver &&
            detail::try_native_affine_composite(ctx, *result, *top, clip, m_mode);
        if (ctx.policy.require_native_gpu &&
            !native_composite && !native_dimension_composite) {
            if (std::getenv("CHRONON3D_PATH_DIAG"))
                spdlog::error("[native-residency-diag] frame={} node={} mode={} operator={} bottom={}x{} origin={},{} handle={} cpu={} top={}x{} origin={},{} handle={} cpu={} opaque={}",
                    static_cast<int>(ctx.frame_input.frame), m_unique_id,
                    static_cast<int>(m_mode), static_cast<int>(m_operator),
                    result->width(), result->height(),
                    result->origin_x(), result->origin_y(), result->surface_handle(),
                    result->data() != nullptr, top->width(), top->height(), top->origin_x(),
                    top->origin_y(), top->surface_handle(), top->data() != nullptr, top->is_opaque());
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
        if (ctx.backend && ctx.backend_stats_api()->supports_native_surfaces() &&
            m_operator == CompositeOperator::SourceOver &&
            !native_composite && !native_dimension_composite) {
            return NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                "CompositeNode",
                "native SourceOver composite failed; CPU fallback is forbidden"};
        }
        if (native_composite || native_dimension_composite) {
            // The native path has already composed the logical surfaces. The
            // CPU pixels remain a reference snapshot for any legacy consumer;
            // the handle is the authoritative value for subsequent native
            // composite nodes.
            result->mark_gpu_authoritative();
        } else if (m_operator != CompositeOperator::SourceOver) {
            // Stencil/Silhouette: use Normal blend to copy top first, then
            // apply the operator via the backend (which handles the masking).
            // The operator is passed along so the backend can apply the
            // appropriate matte-style coverage to the backdrop.
            if (ctx.backend && ctx.surface_registry &&
                ctx.backend_stats_api()->supports_native_surfaces()) {
                if (auto err = detail::materialize_native_or_fail(
                        ctx, *result, *top,
                        "non-SourceOver composite")) {
                    return *err;
                }
            }
            ctx.backend_frame_api()->composite_layer(*result, *top, m_mode, clip, m_operator);
            if (result->surface_handle() != runtime::kInvalidRenderSurfaceHandle)
                result->mark_gpu_authoritative();
    } else {
        // VulkanBackend::composite_layer is a native primitive even when the
        // fast-path probe above declined the optimized SourceOver route
        // (for example Add/stencil operators).  Materialize both handles
        // before dispatching that fallback; otherwise the backend receives
        // the framebuffer sentinel 0 and fails later in resolve_image().
        if (ctx.backend && ctx.surface_registry &&
            ctx.backend_stats_api()->supports_native_surfaces()) {
            if (auto err = detail::materialize_native_or_fail(
                    ctx, *result, *top, "composite fallback")) {
                return *err;
            }
        }
        ctx.backend_frame_api()->composite_layer(*result, *top, m_mode, clip);
        if (result->surface_handle() != runtime::kInvalidRenderSurfaceHandle)
            result->mark_gpu_authoritative();
    }

        // ── Post-blend overhead ────────────────────────────────────────
        _os = profiling::now();

        auto& dof_depth = ctx.node_exec.dof_depth_buffer();
        if (ctx.policy.track_dof_depth && !dof_depth.empty()) {
            const float depth = m_has_world_position
                ? camera_space_depth(ctx.frame_input.camera_2_5d, m_world_position)
                : camera_space_depth(ctx.frame_input.camera_2_5d,
                                     Vec3{0.0f, 0.0f, m_world_z});
            detail::publish_dof_provenance(
                ctx, *top, m_unique_id, depth, detail::dof_source_radius(ctx, depth),
                clip, ctx.frame_input.width, ctx.frame_input.height);
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
