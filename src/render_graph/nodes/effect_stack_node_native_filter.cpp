// Native effect stack implementation unit.
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/effect_gpu_capability.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include "native_surface.hpp"
#include "effect_stack_node_native_api.hpp"
#include "blur_half_res.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <span>
#include <vector>


// Halo / filter dispatch unit: SDF bevel, tint, blur, sharpen and
// directional blur. Glow, bloom, halation and the shadow routes live in
// effect_stack_node_native_detail.cpp.
namespace chronon3d::graph {
namespace native_effects {

bool try_native_full_frame_sdf_bevel(
    RenderGraphContext& ctx, const EffectStack& effect_stack,
    Framebuffer& result, const std::optional<raster::BBox>& clip,
    runtime::RenderSurfaceHandle borrowed_source,
    const Framebuffer* borrowed_source_framebuffer) {
    (void)clip;
    if (!ctx.backend || !ctx.backend_stats_api()->supports_native_surfaces() ||
        !ctx.surface_registry || effect_stack.size() != 1 ||
        result.width() <= 0 || result.height() <= 0) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled ||
        instance.effect_type != effects::EffectType::SdfBevel) {
        return false;
    }
    const auto* params = std::get_if<SdfBevelParams>(&instance.params);
    if (!params) return false;
    const float values[] = {
        params->bevel_width, params->bevel_height, params->rim_width,
        params->rim_intensity, params->highlight, params->shade,
        params->light_angle, params->light_elevation,
        params->light_orbit_speed, params->shadow_distance,
        params->shadow_softness, params->shadow_opacity,
        params->rim_color.r, params->rim_color.g, params->rim_color.b,
        params->rim_color.a, params->shadow_color.r, params->shadow_color.g,
        params->shadow_color.b, params->shadow_color.a};
    for (float value : values) {
        if (!std::isfinite(value)) return false;
    }
    if (params->bevel_width < 0.0f || params->bevel_width > 256.0f ||
        std::abs(params->bevel_height) > 256.0f ||
        params->rim_width < 0.0f || params->rim_width > 128.0f ||
        params->rim_intensity < 0.0f || params->rim_intensity > 4.0f ||
        params->highlight < 0.0f || params->highlight > 4.0f ||
        params->shade < 0.0f || params->shade > 1.0f ||
        params->light_elevation < -90.0f ||
        params->light_elevation > 90.0f ||
        std::abs(params->light_orbit_speed) > 720.0f ||
        params->shadow_distance < 0.0f ||
        params->shadow_distance > 512.0f ||
        params->shadow_softness < 0.0f ||
        params->shadow_softness > 64.0f ||
        params->shadow_opacity < 0.0f ||
        params->shadow_opacity > 1.0f) {
        return false;
    }

    const auto desc = native_surface_desc(ctx, result.width(), result.height());
    const bool source_borrowed =
        borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_needs_expansion = source_borrowed &&
        borrowed_source_framebuffer &&
        (borrowed_source_framebuffer->width() != result.width() ||
         borrowed_source_framebuffer->height() != result.height() ||
         borrowed_source_framebuffer->origin_x() != result.origin_x() ||
         borrowed_source_framebuffer->origin_y() != result.origin_y());
    auto source = source_borrowed ? borrowed_source : result.surface_handle();
    if (source == runtime::kInvalidRenderSurfaceHandle) return false;
    if (source_needs_expansion) {
        const auto expanded = ctx.surface_registry->create(desc);
        if (expanded == runtime::kInvalidRenderSurfaceHandle ||
            !ctx.backend_surface_api()->create_surface(expanded, desc).ok()) {
            if (expanded != runtime::kInvalidRenderSurfaceHandle)
                ctx.surface_registry->release(expanded);
            return false;
        }
        auto expand_op =
            ctx.backend_surface_api()->initialize_transparent_surface(expanded);
        if (expand_op.ok()) {
            const int offset_x =
                borrowed_source_framebuffer->origin_x() - result.origin_x();
            const int offset_y =
                borrowed_source_framebuffer->origin_y() - result.origin_y();
            expand_op = ctx.backend_surface_api()->transform_surface(
                expanded, source, offset_x, offset_y, 1.0f);
        }
        if (!expand_op.ok()) {
            (void)ctx.backend_surface_api()->release_surface(expanded);
            ctx.surface_registry->release(expanded);
            return false;
        }
        source = expanded;
    }

    std::array<runtime::RenderSurfaceHandle, 4> owned{};
    const auto cleanup = [&] {
        if (source_needs_expansion) {
            (void)ctx.backend_surface_api()->release_surface(source);
            ctx.surface_registry->release(source);
        }
        for (auto handle : owned) {
            if (handle != runtime::kInvalidRenderSurfaceHandle) {
                (void)ctx.backend_surface_api()->release_surface(handle);
                ctx.surface_registry->release(handle);
            }
        }
    };
    for (auto& handle : owned) {
        handle = ctx.surface_registry->create(desc);
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            cleanup();
            return false;
        }
    }
    for (auto handle : owned) {
        if (!ctx.backend_surface_api()->create_surface(handle, desc).ok()) {
            cleanup();
            return false;
        }
    }

    const auto [output, scratch_a, scratch_b, scratch_c] = owned;
    const auto dispatched = ctx.backend_surface_api()->sdf_bevel_surfaces(
        output, source, scratch_a, scratch_b, scratch_c, *params,
        ctx.frame_input.time_seconds);
    if (!dispatched.ok()) {
        spdlog::error("[native-sdf-bevel] Vulkan pass failed: {}",
                      dispatched.error().message);
        cleanup();
        return false;
    }
    for (std::size_t i = 1; i < owned.size(); ++i) {
        (void)ctx.backend_surface_api()->release_surface(owned[i]);
        ctx.surface_registry->release(owned[i]);
    }
    if (source_needs_expansion) {
        (void)ctx.backend_surface_api()->release_surface(source);
        ctx.surface_registry->release(source);
    }
    result.set_surface_handle(output);
    result.mark_gpu_authoritative();
    return true;
}

bool try_native_full_frame_tint(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip,
                                runtime::RenderSurfaceHandle borrowed_source) {
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
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

    const auto desc = native_surface_desc(ctx, result.width(), result.height());
    // AUDIT F10 — zero-copy tight-storage path (see the glow fast-path above).
    std::vector<float> packed_rgba;
    const bool source_is_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_is_existing = source_is_borrowed ||
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle;
    const auto source = source_is_borrowed ? borrowed_source :
        (source_is_existing ? result.surface_handle() : ctx.surface_registry->create(desc));
    const auto destination = ctx.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        if (!source_is_existing && source != runtime::kInvalidRenderSurfaceHandle)
            ctx.surface_registry->release(source);
        if (destination != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(destination);
        return false;
    }
    const auto cleanup = [&] {
        if (!source_is_existing) (void)ctx.backend_surface_api()->release_surface(source);
        (void)ctx.backend_surface_api()->release_surface(destination);
        if (!source_is_existing) ctx.surface_registry->release(source);
        ctx.surface_registry->release(destination);
    };
    auto created_source = source_is_existing
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.backend_surface_api()->create_surface(source, desc);
    RenderOpResult uploaded = created_source;
    if (created_source.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        uploaded = ctx.backend_surface_api()->upload_surface(
            source, desc, framebuffer_rgba_view(result, packed_rgba));
    }
    const auto created_destination = uploaded.ok()
        ? ctx.backend_surface_api()->create_surface(destination, desc)
        : uploaded;
    const auto adjusted = created_destination.ok()
        ? ctx.backend_surface_api()->color_adjust_surface(
            destination, source, 0.0f, 1.0f, params->color,
            params->color.a * params->amount)
        : created_destination;
    if (!adjusted.ok()) {
        cleanup();
        return false;
    }
    if (!source_is_borrowed) {
        (void)ctx.backend_surface_api()->release_surface(source);
        ctx.surface_registry->release(source);
    }
    result.set_surface_handle(destination);
    result.mark_gpu_authoritative();
    return true;
}

bool try_native_full_frame_blur(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip,
                                runtime::RenderSurfaceHandle borrowed_source,
                                const Framebuffer* borrowed_source_framebuffer) {
    if (!ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::Blur) return false;
    const auto* params = std::get_if<BlurParams>(&instance.params);
    // The authored blur radius is bounded to 64. Its two-pass-equivalent
    // box kernel can be wider, so the backend validates the derived radius
    // separately instead of rejecting it before native dispatch.
    if (!params || !std::isfinite(params->radius) ||
        params->radius < 0.0f || params->radius > 128.0f) {
        return false;
    }
    // Fast Box Blur is CPU-authoritative until the native path loops passes;
    // strict native fails closed instead of silently running one Gaussian pass.
    if (params->box) return false;
    const auto desc = native_surface_desc(ctx, result.width(), result.height());
    const bool source_is_borrowed =
        borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_needs_expansion = source_is_borrowed && borrowed_source_framebuffer &&
        (borrowed_source_framebuffer->width() != result.width() ||
         borrowed_source_framebuffer->height() != result.height() ||
         borrowed_source_framebuffer->origin_x() != result.origin_x() ||
         borrowed_source_framebuffer->origin_y() != result.origin_y());
    const bool source_is_existing = !source_needs_expansion &&
        (source_is_borrowed || result.surface_handle() != runtime::kInvalidRenderSurfaceHandle);
    const auto source = source_is_existing
        ? (source_is_borrowed ? borrowed_source : result.surface_handle())
        : ctx.surface_registry->create(desc);
    const auto horizontal = ctx.surface_registry->create(desc);
    const auto output = ctx.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        horizontal == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        if ((!source_is_existing || source_needs_expansion) && source != runtime::kInvalidRenderSurfaceHandle)
            ctx.surface_registry->release(source);
        if (horizontal != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(horizontal);
        if (output != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(output);
        return false;
    }
    const auto cleanup = [&] {
        if (!source_is_existing || source_needs_expansion) {
            (void)ctx.backend_surface_api()->release_surface(source);
            ctx.surface_registry->release(source);
        }
        (void)ctx.backend_surface_api()->release_surface(horizontal);
        (void)ctx.backend_surface_api()->release_surface(output);
        ctx.surface_registry->release(horizontal);
        ctx.surface_registry->release(output);
    };
    const auto source_ready = source_is_existing
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.backend_surface_api()->create_surface(source, desc);
    RenderOpResult uploaded = source_ready;
    if (source_ready.ok() && source_needs_expansion) {
        uploaded = ctx.backend_surface_api()->initialize_transparent_surface(source);
        if (uploaded.ok()) {
            const int offset_x = borrowed_source_framebuffer->origin_x() - result.origin_x();
            const int offset_y = borrowed_source_framebuffer->origin_y() - result.origin_y();
            uploaded = ctx.backend_surface_api()->transform_surface(source, borrowed_source, offset_x, offset_y, 1.0f);
        }
    } else if (source_ready.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        // AUDIT F10 — zero-copy tight-storage path (see the glow fast-path).
        std::vector<float> packed_rgba;
        uploaded = ctx.backend_surface_api()->upload_surface(
            source, desc, framebuffer_rgba_view(result, packed_rgba));
    }
    const auto created_horizontal = uploaded.ok()
        ? ctx.backend_surface_api()->create_surface(horizontal, desc)
        : uploaded;
    const auto created_output = created_horizontal.ok()
        ? ctx.backend_surface_api()->create_surface(output, desc)
        : created_horizontal;
    const bool partial_clip = clip && !clip_covers_result(clip, result);
    std::optional<raster::BBox> local_clip;
    if (partial_clip) {
        const auto halo = static_cast<i32>(std::ceil(
            effects::spatial_halo_extent(
                effects::blur_spatial_halo(params->radius, 2))));
        local_clip = raster::BBox{
            clip->x0 - halo - result.origin_x(),
            clip->y0 - halo - result.origin_y(),
            clip->x1 + halo - result.origin_x(),
            clip->y1 + halo - result.origin_y()};
        local_clip->clip_to(result.width(), result.height());
    }
    const auto rounded_radius = std::max(1, static_cast<int>(std::round(params->radius)));
    const float r_plus = static_cast<float>(rounded_radius * rounded_radius + rounded_radius);
    const int box_radius = std::max(1, static_cast<int>(std::round(
        0.5f * (std::sqrt(1.0f + 6.0f * r_plus) - 1.0f))));
    const auto box_radius_for = [](float radius) {
        const int rounded = std::max(1, static_cast<int>(std::round(radius)));
        const float plus = static_cast<float>(rounded * rounded + rounded);
        return std::max(1, static_cast<int>(std::round(
            0.5f * (std::sqrt(1.0f + 6.0f * plus) - 1.0f))));
    };
    // Half-resolution fast path: the measured production clips spend 62–77% of
    // their GPU pass time in full-frame blur (up to 40 passes/frame). A blur is
    // a low-pass filter, so computing it on a half-size plate and upscaling
    // removes 75% of the pixels each pass touches. Eligibility (radius, clip,
    // origin, canvas size, opt-out) is owned by blur_half_res.hpp; this block
    // owns only the surfaces and the dispatch order.
    const bool half_res = detail::blur_half_res_eligible(
        result.width(), result.height(), params->radius, partial_clip,
        result.origin_x() == 0 && result.origin_y() == 0,
        detail::blur_half_res_env_enabled());
    const auto try_half_res_blur = [&]() -> bool {
        if (!created_output.ok()) return false;
        const i32 half_w = std::max(1, result.width() / 2);
        const i32 half_h = std::max(1, result.height() / 2);
        const auto half_desc = native_surface_desc(ctx, half_w, half_h);
        const runtime::RenderSurfaceHandle half_source = ctx.surface_registry->create(half_desc);
        const runtime::RenderSurfaceHandle half_scratch = ctx.surface_registry->create(half_desc);
        const runtime::RenderSurfaceHandle half_blurred = ctx.surface_registry->create(half_desc);
        const std::array<runtime::RenderSurfaceHandle, 3> half_surfaces{
            half_source, half_scratch, half_blurred};
        std::array<bool, 3> half_device_created{false, false, false};
        const auto release_half = [&] {
            for (std::size_t index = 0; index < half_surfaces.size(); ++index) {
                const auto handle = half_surfaces[index];
                if (handle == runtime::kInvalidRenderSurfaceHandle) continue;
                if (half_device_created[index]) {
                    (void)ctx.backend_surface_api()->release_surface(handle);
                }
                ctx.surface_registry->release(handle);
            }
        };
        if (half_source == runtime::kInvalidRenderSurfaceHandle ||
            half_scratch == runtime::kInvalidRenderSurfaceHandle ||
            half_blurred == runtime::kInvalidRenderSurfaceHandle) {
            release_half();
            return false;
        }
        for (std::size_t index = 0; index < half_surfaces.size(); ++index) {
            const auto created = ctx.backend_surface_api()->create_surface(
                half_surfaces[index], half_desc);
            if (!created.ok()) {
                release_half();
                return false;
            }
            half_device_created[index] = true;
        }
        // Scale a full-frame 0-based surface into a half-size one, and back.
        // Bilinear=1 is mandatory: nearest sampling would alias the blur.
        const auto scale_into = [&](runtime::RenderSurfaceHandle dst,
                                    runtime::RenderSurfaceHandle src,
                                    i32 dst_w, i32 dst_h, i32 src_w, i32 src_h,
                                    float sx, float sy) {
            runtime::SurfaceAffineTransform mapping{};
            runtime::set_canvas_source_mapping(
                mapping, sx, 0.0f, 0.0f, 0.0f, sy, 0.0f,
                runtime::RectF{0.0f, 0.0f,
                               static_cast<float>(src_w), static_cast<float>(src_h)});
            mapping.opacity = 1.0f;
            mapping.bilinear = 1u;
            mapping.clip_enabled = 1u;
            mapping.clip_rect[0] = 0;
            mapping.clip_rect[1] = 0;
            mapping.clip_rect[2] = dst_w;
            mapping.clip_rect[3] = dst_h;
            return ctx.backend_surface_api()->transform_surface_affine(dst, src, mapping);
        };
        auto op = scale_into(half_source, source, half_w, half_h,
                             result.width(), result.height(), 2.0f, 2.0f);
        const int half_box_radius = box_radius_for(params->radius * 0.5f);
        if (op.ok()) op = ctx.backend_surface_api()->blur_surface(
            half_scratch, half_source, static_cast<float>(half_box_radius),
            /*horizontal=*/true, /*box_kernel=*/true);
        if (op.ok()) op = ctx.backend_surface_api()->blur_surface(
            half_blurred, half_scratch, static_cast<float>(half_box_radius),
            /*horizontal=*/false, /*box_kernel=*/true);
        if (op.ok()) op = ctx.backend_surface_api()->blur_surface(
            half_scratch, half_blurred, static_cast<float>(half_box_radius),
            /*horizontal=*/true, /*box_kernel=*/true);
        if (op.ok()) op = ctx.backend_surface_api()->blur_surface(
            half_blurred, half_scratch, static_cast<float>(half_box_radius),
            /*horizontal=*/false, /*box_kernel=*/true);
        if (op.ok()) op = scale_into(output, half_blurred, result.width(), result.height(),
                                     half_w, half_h, 0.5f, 0.5f);
        release_half();
        if (!op.ok()) {
            spdlog::warn("[native-blur] half-res radius={} failed: {} — falling back to full-res",
                         params->radius, op.error().message);
            return false;
        }
        return true;
    };
    RenderOpResult blurred_vertical = half_res
        ? (try_half_res_blur()
               ? RenderOpResult(RenderOpOutcome{})
               : RenderOpResult(graph::make_render_backend_error(
                     graph::RenderBackendErrorCode::ExecutionFailure,
                     "native full-frame blur: half-res path rejected")))
        : created_output;
    if (!half_res || !blurred_vertical.ok()) {
        // Full-resolution path (historical): also the fallback when the
        // half-res surfaces or affine dispatch are unavailable.
        if (half_res) {
            spdlog::warn("[native-blur] half-res unavailable for radius={} — using full-res",
                         params->radius);
        }
        blurred_vertical = created_output.ok()
            ? ctx.backend_surface_api()->blur_surface(horizontal, source, static_cast<float>(box_radius),
                                        /*horizontal=*/true, /*box_kernel=*/true)
            : created_output;
        blurred_vertical = blurred_vertical.ok()
            ? ctx.backend_surface_api()->blur_surface(output, horizontal, static_cast<float>(box_radius),
                                        /*horizontal=*/false, /*box_kernel=*/true)
            : blurred_vertical;
        blurred_vertical = blurred_vertical.ok()
            ? ctx.backend_surface_api()->blur_surface(
                horizontal, output, static_cast<float>(box_radius),
                /*horizontal=*/true, /*box_kernel=*/true)
            : blurred_vertical;
        blurred_vertical = blurred_vertical.ok()
            ? ctx.backend_surface_api()->blur_surface(
                output, horizontal, static_cast<float>(box_radius),
                /*horizontal=*/false, /*box_kernel=*/true)
            : blurred_vertical;
    }
    const auto clipped_result = blurred_vertical.ok() && partial_clip
        ? ctx.backend_surface_api()->copy_surface(source, output, local_clip)
        : blurred_vertical;
    if (!clipped_result.ok()) {
        spdlog::error("[native-blur] radius={} derived_box_radius={} failed: {}",
                      params->radius, box_radius, clipped_result.error().message);
        cleanup();
        return false;
    }
    (void)ctx.backend_surface_api()->release_surface(horizontal);
    ctx.surface_registry->release(horizontal);
    if (partial_clip) {
        (void)ctx.backend_surface_api()->release_surface(output);
        ctx.surface_registry->release(output);
        result.set_surface_handle(source);
    } else {
        if (!source_is_borrowed || source_needs_expansion) {
            (void)ctx.backend_surface_api()->release_surface(source);
            ctx.surface_registry->release(source);
        }
        result.set_surface_handle(output);
    }
    result.mark_gpu_authoritative();
    return true;
}

// Native Sharpen is an unsharp mask over the certified separable box-filter
// route: the blurred surface is produced by the same four box passes the
// native blur certification covers (radius 0..10, full frame), then a single
// pointwise combine applies amount/threshold exactly like the CPU oracle.
// Partial clips fail closed to the software processor.
bool try_native_full_frame_sharpen(RenderGraphContext& ctx,
                                   const EffectStack& effect_stack,
                                   Framebuffer& result,
                                   const std::optional<raster::BBox>& clip,
                                   runtime::RenderSurfaceHandle borrowed_source,
                                   const Framebuffer* borrowed_source_framebuffer) {
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::Sharpen) return false;
    const auto* params = std::get_if<SharpenParams>(&instance.params);
    // Mirrors the CPU oracle gates, restricted to the certified blur branch:
    // radius (0, 10] stays on SeparableFullRes, nonzero amount, finite
    // threshold. amount == 0 is the CPU identity fast path.
    if (!params || !std::isfinite(params->amount) || params->amount == 0.0f ||
        std::abs(params->amount) > 4.0f || !std::isfinite(params->radius) ||
        params->radius <= 0.0f || params->radius > 10.0f ||
        !std::isfinite(params->threshold) || params->threshold < 0.0f ||
        params->threshold > 1.0f) {
        return false;
    }
    const auto desc = native_surface_desc(ctx, result.width(), result.height());
    const bool has_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool has_result_surface =
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend_surface_api()->is_native_surface_valid(result.surface_handle());
    if (!has_borrowed && !has_result_surface) {
        return false;
    }
    AnchoredSource anchored;
    if (has_borrowed &&
        !anchor_source_to_result(ctx, result, borrowed_source,
                                 borrowed_source_framebuffer, anchored)) {
        return false;
    }
    const auto source = anchored.surface != runtime::kInvalidRenderSurfaceHandle
        ? anchored.surface
        : result.surface_handle();
    const auto anchor_cleanup = [&] { release_anchored_source(ctx, anchored); };
    const auto scratch_a = ctx.surface_registry->create(desc);
    const auto scratch_b = ctx.surface_registry->create(desc);
    const auto blurred = ctx.surface_registry->create(desc);
    const auto destination = ctx.surface_registry->create(desc);
    if (scratch_a == runtime::kInvalidRenderSurfaceHandle ||
        scratch_b == runtime::kInvalidRenderSurfaceHandle ||
        blurred == runtime::kInvalidRenderSurfaceHandle ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        if (scratch_a != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(scratch_a);
        if (scratch_b != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(scratch_b);
        if (blurred != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(blurred);
        if (destination != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(destination);
        anchor_cleanup();
        return false;
    }
    const auto release_scratch = [&] {
        (void)ctx.backend_surface_api()->release_surface(scratch_a);
        (void)ctx.backend_surface_api()->release_surface(scratch_b);
        (void)ctx.backend_surface_api()->release_surface(blurred);
        ctx.surface_registry->release(scratch_a);
        ctx.surface_registry->release(scratch_b);
        ctx.surface_registry->release(blurred);
    };
    const int rounded = std::max(1, static_cast<int>(std::round(params->radius)));
    const double r_plus = static_cast<double>(rounded) * rounded + rounded;
    const int box_radius = std::max(1, static_cast<int>(std::round(
        0.5 * (std::sqrt(1.0 + 6.0 * r_plus) - 1.0))));
    const float box = static_cast<float>(box_radius);
    const auto created = ctx.backend_surface_api()->create_surface(scratch_a, desc).ok()
        ? ctx.backend_surface_api()->create_surface(scratch_b, desc)
        : RenderOpResult(RenderOpOutcome{});
    const auto ready = created.ok() ? ctx.backend_surface_api()->create_surface(blurred, desc) : created;
    const auto dst_ready = ready.ok() ? ctx.backend_surface_api()->create_surface(destination, desc) : ready;
    if (!dst_ready.ok()) {
        release_scratch();
        ctx.surface_registry->release(destination);
        anchor_cleanup();
        return false;
    }
    const auto blurred_ok = ctx.backend_surface_api()->blur_surface(scratch_a, source, box, true, true).ok()
        ? ctx.backend_surface_api()->blur_surface(scratch_b, scratch_a, box, false, true)
        : RenderOpResult(RenderOpOutcome{});
    const auto blurred_done = blurred_ok.ok()
        ? ctx.backend_surface_api()->blur_surface(scratch_a, scratch_b, box, true, true)
        : blurred_ok;
    const auto blurred_final = blurred_done.ok()
        ? ctx.backend_surface_api()->blur_surface(blurred, scratch_a, box, false, true)
        : blurred_done;
    const auto combined = blurred_final.ok()
        ? ctx.backend_surface_api()->sharpen_combine_surface(destination, source, blurred,
                                               params->amount, params->threshold)
        : blurred_final;
    if (!combined.ok()) {
        release_scratch();
        (void)ctx.backend_surface_api()->release_surface(destination);
        ctx.surface_registry->release(destination);
        anchor_cleanup();
        return false;
    }
    release_scratch();
    anchor_cleanup();
    result.set_surface_handle(destination);
    result.mark_gpu_authoritative();
    return true;
}

// Native DirectionalBlur is a single-pass uniform box-tap blur along the
// authored direction, matching the software processor tap-for-tap
// (generate_taps formula, Clamp-edge bilinear, uniform 1/n weights).
// samples == 1 is the pinned single-tap identity and forwards like length 0.
// Certified window: full frame only (partial clips fail closed to software),
// finite angle, length in (0, 64], tap count in [2, 64] where taps =
// explicit samples when > 1 else max(2, ceil(length / 2)). Length <= 0 is the
// CPU identity fast path and is forwarded without dispatch. The unit
// direction is computed on the host so the shader performs no trigonometry.
bool try_native_full_frame_directional_blur(RenderGraphContext& ctx,
                                            const EffectStack& effect_stack,
                                            Framebuffer& result,
                                            const std::optional<raster::BBox>& clip,
                                            runtime::RenderSurfaceHandle borrowed_source) {
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::DirectionalBlur) return false;
    const auto* params = std::get_if<DirectionalBlurParams>(&instance.params);
    if (!params || !std::isfinite(params->angle)) return false;
    const bool has_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool has_result_surface =
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend_surface_api()->is_native_surface_valid(result.surface_handle());
    if (!has_borrowed && !has_result_surface) return false;
    if (!(params->length > 0.0f)) {
        if (!std::isfinite(params->length)) return false;
        // CPU identity fast path: forward the input surface untouched.
        if (has_borrowed) {
            result.set_surface_handle(borrowed_source);
            result.mark_gpu_authoritative();
        }
        return true;
    }
    if (params->length > 64.0f) return false;
    if (params->samples == 1) {
        // Pinned CPU identity: single center tap.
        if (has_borrowed) {
            result.set_surface_handle(borrowed_source);
            result.mark_gpu_authoritative();
        }
        return true;
    }
    const int taps = (params->samples > 1)
        ? params->samples
        : std::max(2, static_cast<int>(std::ceil(params->length / 2.0f)));
    if (taps < 2 || taps > 64) return false;
    if (params->samples < 0 || params->samples > 64) return false;
    const float radians = params->angle * (3.14159265358979323846f / 180.0f);
    const float dir_x = std::cos(radians);
    const float dir_y = std::sin(radians);
    if (!std::isfinite(dir_x) || !std::isfinite(dir_y)) return false;
    const auto source = has_borrowed ? borrowed_source : result.surface_handle();
    const auto desc = native_surface_desc(ctx, result.width(), result.height());
    const auto destination = ctx.surface_registry->create(desc);
    if (destination == runtime::kInvalidRenderSurfaceHandle) return false;
    const auto created = ctx.backend_surface_api()->create_surface(destination, desc);
    if (!created.ok()) {
        ctx.surface_registry->release(destination);
        return false;
    }
    const auto dispatched = ctx.backend_surface_api()->directional_blur_surface(
        destination, source, dir_x, dir_y, params->length, taps);
    if (!dispatched.ok()) {
        (void)ctx.backend_surface_api()->release_surface(destination);
        ctx.surface_registry->release(destination);
        return false;
    }
    result.set_surface_handle(destination);
    result.mark_gpu_authoritative();
    return true;
}


} // namespace native_effects

} // namespace chronon3d::graph
