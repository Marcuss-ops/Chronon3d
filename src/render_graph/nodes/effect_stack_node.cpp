// ============================================================================
// effect_stack_node.cpp — EffectStackNode implementation.
//
// Extracted from effect_stack_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <vector>

#include "native_surface.hpp"

namespace chronon3d::graph {

namespace {

bool clip_covers_result(const std::optional<raster::BBox>& clip,
                        const Framebuffer& result) {
    if (!clip) return true;
    const raster::BBox bounds{
        result.origin_x(), result.origin_y(),
        result.origin_x() + result.width(),
        result.origin_y() + result.height()};
    return clip->x0 <= bounds.x0 && clip->y0 <= bounds.y0 &&
           clip->x1 >= bounds.x1 && clip->y1 >= bounds.y1;
}

bool try_native_full_frame_glow(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip) {
    if (!clip_covers_result(clip, result) || !ctx.services.backend || !ctx.services.surface_registry ||
        effect_stack.size() != 1 ||
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::Glow) return false;
    const auto* params = std::get_if<GlowParams>(&instance.params);
    if (!params || params->layers.size() != 0 || !params->preserve_source ||
        params->blend != BlendMode::Add || params->threshold != 0.0f ||
        params->spread != 1.0f || params->softness != 1.0f ||
        params->falloff != 0.85f || params->core_strength != 0.70f ||
        params->aura_strength != 0.35f || params->bloom_strength != 0.18f ||
        params->outer_downscale != 0.25f || !std::isfinite(params->radius) ||
        !std::isfinite(params->intensity) || params->radius < 0.0f ||
        params->radius > 32.0f || params->intensity < 0.0f) {
        return false;
    }

    const auto desc = native_surface_desc(result.width(), result.height());
    const auto rgba = pack_framebuffer_rgba(result);
    const auto output = ctx.services.surface_registry->create(desc);
    const auto horizontal = ctx.services.surface_registry->create(desc);
    const auto vertical = ctx.services.surface_registry->create(desc);
    if (output == runtime::kInvalidRenderSurfaceHandle ||
        horizontal == runtime::kInvalidRenderSurfaceHandle ||
        vertical == runtime::kInvalidRenderSurfaceHandle) {
        if (output != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(output);
        if (horizontal != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(horizontal);
        if (vertical != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(vertical);
        return false;
    }
    const auto cleanup = [&] {
        (void)ctx.services.backend->release_surface(output);
        (void)ctx.services.backend->release_surface(horizontal);
        (void)ctx.services.backend->release_surface(vertical);
        ctx.services.surface_registry->release(output);
        ctx.services.surface_registry->release(horizontal);
        ctx.services.surface_registry->release(vertical);
    };
    const auto create_output = ctx.services.backend->create_surface(output, desc);
    const auto upload = create_output.ok()
        ? ctx.services.backend->upload_surface(output, desc, rgba)
        : create_output;
    const auto create_horizontal = upload.ok()
        ? ctx.services.backend->create_surface(horizontal, desc)
        : upload;
    const auto create_vertical = create_horizontal.ok()
        ? ctx.services.backend->create_surface(vertical, desc)
        : create_horizontal;
    const auto glow = create_vertical.ok()
        ? ctx.services.backend->glow_surfaces(
            output, output, horizontal, vertical, params->radius,
            params->intensity, params->color)
        : create_vertical;
    if (!glow.ok()) {
        cleanup();
        return false;
    }
    (void)ctx.services.backend->release_surface(horizontal);
    (void)ctx.services.backend->release_surface(vertical);
    ctx.services.surface_registry->release(horizontal);
    ctx.services.surface_registry->release(vertical);
    result.set_surface_handle(output);
    return true;
}

bool try_native_full_frame_tint(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip) {
    if (!clip_covers_result(clip, result) || !ctx.services.backend || !ctx.services.surface_registry ||
        effect_stack.size() != 1 ||
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::Tint) return false;
    const auto* params = std::get_if<TintParams>(&instance.params);
    if (!params || !std::isfinite(params->amount) || !std::isfinite(params->color.r) ||
        !std::isfinite(params->color.g) || !std::isfinite(params->color.b) ||
        !std::isfinite(params->color.a) || params->amount < 0.0f ||
        params->amount > 1.0f || params->color.a <= 0.0f) {
        return false;
    }

    const auto desc = native_surface_desc(result.width(), result.height());
    const auto rgba = pack_framebuffer_rgba(result);
    const auto source = ctx.services.surface_registry->create(desc);
    const auto destination = ctx.services.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        if (source != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(source);
        if (destination != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(destination);
        return false;
    }
    const auto cleanup = [&] {
        (void)ctx.services.backend->release_surface(source);
        (void)ctx.services.backend->release_surface(destination);
        ctx.services.surface_registry->release(source);
        ctx.services.surface_registry->release(destination);
    };
    const auto created_source = ctx.services.backend->create_surface(source, desc);
    const auto uploaded = created_source.ok()
        ? ctx.services.backend->upload_surface(source, desc, rgba)
        : created_source;
    const auto created_destination = uploaded.ok()
        ? ctx.services.backend->create_surface(destination, desc)
        : uploaded;
    const auto adjusted = created_destination.ok()
        ? ctx.services.backend->color_adjust_surface(
            destination, source, 0.0f, 1.0f, params->color,
            params->color.a * params->amount)
        : created_destination;
    if (!adjusted.ok()) {
        cleanup();
        return false;
    }
    (void)ctx.services.backend->release_surface(source);
    ctx.services.surface_registry->release(source);
    result.set_surface_handle(destination);
    return true;
}

bool try_native_full_frame_blur(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip) {
    if (!clip_covers_result(clip, result) || !ctx.services.backend || !ctx.services.surface_registry ||
        effect_stack.size() != 1 ||
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::Blur) return false;
    const auto* params = std::get_if<BlurParams>(&instance.params);
    if (!params || !std::isfinite(params->radius) ||
        params->radius < 0.0f || params->radius > 32.0f) {
        return false;
    }

    const auto desc = native_surface_desc(result.width(), result.height());
    const auto rgba = pack_framebuffer_rgba(result);
    const auto source = ctx.services.surface_registry->create(desc);
    const auto horizontal = ctx.services.surface_registry->create(desc);
    const auto output = ctx.services.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        horizontal == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        if (source != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(source);
        if (horizontal != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(horizontal);
        if (output != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(output);
        return false;
    }
    const auto cleanup = [&] {
        (void)ctx.services.backend->release_surface(source);
        (void)ctx.services.backend->release_surface(horizontal);
        (void)ctx.services.backend->release_surface(output);
        ctx.services.surface_registry->release(source);
        ctx.services.surface_registry->release(horizontal);
        ctx.services.surface_registry->release(output);
    };
    const auto created_source = ctx.services.backend->create_surface(source, desc);
    const auto uploaded = created_source.ok()
        ? ctx.services.backend->upload_surface(source, desc, rgba)
        : created_source;
    const auto created_horizontal = uploaded.ok()
        ? ctx.services.backend->create_surface(horizontal, desc)
        : uploaded;
    const auto created_output = created_horizontal.ok()
        ? ctx.services.backend->create_surface(output, desc)
        : created_horizontal;
    const auto blurred_horizontal = created_output.ok()
        ? ctx.services.backend->blur_surface(horizontal, source, params->radius, /*horizontal=*/true)
        : created_output;
    const auto blurred_vertical = blurred_horizontal.ok()
        ? ctx.services.backend->blur_surface(output, horizontal, params->radius, /*horizontal=*/false)
        : blurred_horizontal;
    if (!blurred_vertical.ok()) {
        cleanup();
        return false;
    }
    (void)ctx.services.backend->release_surface(source);
    (void)ctx.services.backend->release_surface(horizontal);
    ctx.services.surface_registry->release(source);
    ctx.services.surface_registry->release(horizontal);
    result.set_surface_handle(output);
    return true;
}

} // namespace

std::optional<raster::BBox> EffectStackNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    if (input_bboxes.empty() || !input_bboxes[0]) {
        return std::nullopt;
    }
    auto bbox = *input_bboxes[0];
    if (bbox.is_empty()) {
        // When the input bbox is empty (e.g. a degenerate shape whose
        // corners all project to w≈0, producing an inverted {max,max,min,min}
        // bbox that collapses to {0,0,0,0} after SourceNode clipping),
        // returning it as a valid BBox causes execute() to intersect it
        // with the clip rect → inverted local_clip → negative ROI
        // dimensions → acquire_temp_framebuffer crash.  Return nullopt
        // so execute() falls back to the full input framebuffer + the
        // executor-provided clip_rect, which is always well-formed.
        return std::nullopt;
    }
    const f32 spread = compute_max_effect_spread();
    if (spread <= 0.0f) {
        return bbox;
    }
    bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
    bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
    bbox.x1 = std::min(ctx.frame_input.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
    bbox.y1 = std::min(ctx.frame_input.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));

    if (ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[EffectStackNode] input_bbox=({}, {})-({}, {}) spread={} output_bbox=({}, {})-({}, {})",
            input_bboxes[0]->x0,
            input_bboxes[0]->y0,
            input_bboxes[0]->x1,
            input_bboxes[0]->y1,
            spread,
            bbox.x0,
            bbox.y0,
            bbox.x1,
            bbox.y1
        );
    }

    // After spread expansion + clamping, the bbox could still be empty
    // (e.g. input was inverted and expansion didn't fix it).  Return
    // nullopt so execute() falls back to the full input framebuffer.
    if (bbox.is_empty()) {
        return std::nullopt;
    }
    return bbox;
}

NodeExecResult EffectStackNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        empty->clear(Color::transparent());
        return NodeExecResult{std::move(empty)};
    }

    const f32 spread = compute_max_effect_spread();
    auto pred_bbox = predicted_bbox(ctx, input_bboxes);

    OwnedFB result;
    if (spread > 0.0f && pred_bbox) {
        const raster::BBox out_bounds = *pred_bbox;
        const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
        const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);

        result = ctx.acquire_owned_fb(out_w, out_h, true, out_bounds);

        const i32 intersect_x0 = std::max(inputs[0]->origin_x(), out_bounds.x0);
        const i32 intersect_y0 = std::max(inputs[0]->origin_y(), out_bounds.y0);
        const i32 intersect_x1 = std::min(inputs[0]->origin_x() + inputs[0]->width(), out_bounds.x1);
        const i32 intersect_y1 = std::min(inputs[0]->origin_y() + inputs[0]->height(), out_bounds.y1);

        if (intersect_x1 > intersect_x0 && intersect_y1 > intersect_y0) {
            const i32 w_copy = intersect_x1 - intersect_x0;
            for (i32 y = intersect_y0; y < intersect_y1; ++y) {
                const Color* src = inputs[0]->pixels_row(y - inputs[0]->origin_y()) + (intersect_x0 - inputs[0]->origin_x());
                Color* dst = result->pixels_row(y - out_bounds.y0) + (intersect_x0 - out_bounds.x0);
                std::copy_n(src, w_copy, dst);
            }
        }

        result->set_opaque(inputs[0]->is_opaque());
        result->set_key_digest(inputs[0]->key_digest());
    } else {
        result = ctx.acquire_owned_fb(*inputs[0]);
        // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — when spread==0, the
        // output framebuffer is the input framebuffer acquired full-frame.
        // The FramebufferPool's swap_contents placeholder pattern is
        // zero-copy (no byte memcpy) so we increment PASS-only here (every
        // pixel touched via metadata swap). The byte-side copy fallback
        // IS counted at the canonical std::copy site in
        // `framebuffer_acquire.cpp::acquire_framebuffer(const Framebuffer&)`
        // where the pool returns a re-used allocation (data ptr diff) and
        // std::copy runs. Splitting PASS from COPIES like this matches the
        // gate semantic: B03 CinematicGlow1080p must reach
        // `full_frame_copies_per_frame == 0` in steady state because the
        // glow spread drives bbox-dilated writes via the branch above (no
        // equivalent std::copy fallback fires).
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (ctx.services.backend) {
        std::optional<raster::BBox> local_clip = ctx.node_exec.clip_rect;
        if (pred_bbox) {
            if (local_clip) {
                local_clip->x0 = std::max(local_clip->x0, pred_bbox->x0);
                local_clip->y0 = std::max(local_clip->y0, pred_bbox->y0);
                local_clip->x1 = std::min(local_clip->x1, pred_bbox->x1);
                local_clip->y1 = std::min(local_clip->y1, pred_bbox->y1);
            } else {
                local_clip = pred_bbox;
            }
            // Guard: if the intersection produced an inverted clip
            // (x0 > x1 or y0 > y1), discard it so effect
            // implementations don't receive invalid region bounds.
            if (local_clip && local_clip->is_empty()) {
                local_clip = std::nullopt;
            }
        }
        // Keep the clip in canvas coordinates here.  SoftwareBackend is the
        // single boundary responsible for translating a clip into the
        // framebuffer-local ROI; translating here as well would subtract the
        // ROI origin twice and silently disable effects on cropped layers.
        const effects::EffectExecutionContext effect_context{
            .time_seconds = ctx.frame_input.time_seconds,
            .frame = ctx.frame_input.frame,
            .clip = local_clip,
            .quality = effects::RenderQuality::Final,
            .diagnostics_enabled = ctx.policy.diagnostics_enabled,                .effect_processors = ctx.node_exec.current_effect_processors,
                .processor_snapshot = ctx.node_exec.processor_snapshot,
                .processors_resolved = ctx.node_exec.processor_bindings_compiled

        };
        if (!try_native_full_frame_blur(ctx, m_effects, *result, local_clip) &&
            !try_native_full_frame_glow(ctx, m_effects, *result, local_clip) &&
            !try_native_full_frame_tint(ctx, m_effects, *result, local_clip)) {
            ctx.services.backend->apply_effect_stack(*result, m_effects, effect_context);
        }
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = static_cast<uint64_t>(ctx.frame_input.width * ctx.frame_input.height);
            if (local_clip) {
                raster::BBox clipped = *local_clip;
                clipped.clip_to(ctx.frame_input.width, ctx.frame_input.height);
                if (!clipped.is_empty()) {
                    area = static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
                } else {
                    area = 0;
                }
            }
            ctx.node_exec.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
        }
    }
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
