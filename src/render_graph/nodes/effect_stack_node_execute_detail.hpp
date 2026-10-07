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
    const bool has_echo = std::any_of(m_effects.begin(), m_effects.end(),
        [](const EffectInstance& effect) {
            return effect.enabled && effect.effect_type == effects::EffectType::Echo;
        });
    auto pred_bbox = predicted_bbox(ctx, input_bboxes);
    // A strict native export may carry a stale CPU-side allocation from a
    // reused physical slot while the Vulkan surface is already authoritative.
    // The native handle, not the optional CPU pointer, decides residency here;
    // otherwise a tight projected text surface is incorrectly expanded through
    // the CPU crop path and attempts to resize an external framebuffer.
    const bool gpu_only_input =
        ctx.policy.require_native_gpu &&
        inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        (!ctx.backend || ctx.backend->is_native_surface_valid(
            inputs[0]->surface_handle()));
    OwnedFB result;
    if (has_echo) {
        // Echo needs a stable canvas coordinate system: tight per-frame layer
        // bounds move as the source animates, while its history is sampled by
        // frame. Expand the source into a transparent canvas before recording
        // or compositing temporal samples so trails survive those bound shifts.
        result = ctx.acquire_owned_fb(
            ctx.frame_input.width, ctx.frame_input.height, !gpu_only_input);
        result->set_origin(0, 0);
        if (!gpu_only_input && result->data()) {
            result->clear(Color::transparent());
        }
        const auto& source = *inputs[0];
        const i32 dst_x0 = std::max(0, source.origin_x());
        const i32 dst_y0 = std::max(0, source.origin_y());
        const i32 dst_x1 = std::min(ctx.frame_input.width, source.origin_x() + source.width());
        const i32 dst_y1 = std::min(ctx.frame_input.height, source.origin_y() + source.height());
        if (!gpu_only_input && dst_x1 > dst_x0 && dst_y1 > dst_y0 &&
            result->data() && source.data()) {
            for (i32 y = dst_y0; y < dst_y1; ++y) {
                const Color* src = source.pixels_row(y - source.origin_y()) + (dst_x0 - source.origin_x());
                Color* dst = result->pixels_row(y) + dst_x0;
                std::copy_n(src, dst_x1 - dst_x0, dst);
            }
        }
        result->set_opaque(false);
        result->set_key_digest(source.key_digest());
    } else if (spread > 0.0f && pred_bbox) {
        const raster::BBox out_bounds = *pred_bbox;
        const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
        const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);

        if (gpu_only_input) {
            // Native video execution keeps the producer surface GPU-only.
            // Native video execution keeps the producer surface GPU-only. Use
            // a canvas-sized destination so the expanded source has a stable
            // canvas coordinate system even when the predicted tight bbox is
            // expressed in a different local space.
            result = ctx.acquire_owned_fb(
                ctx.frame_input.width, ctx.frame_input.height, false);
            result->set_origin(0, 0);
        } else {
            result = ctx.acquire_owned_fb(out_w, out_h, true, out_bounds);
        }

        const i32 intersect_x0 = std::max(inputs[0]->origin_x(), out_bounds.x0);
        const i32 intersect_y0 = std::max(inputs[0]->origin_y(), out_bounds.y0);
        const i32 intersect_x1 = std::min(inputs[0]->origin_x() + inputs[0]->width(), out_bounds.x1);
        const i32 intersect_y1 = std::min(inputs[0]->origin_y() + inputs[0]->height(), out_bounds.y1);

        if (!gpu_only_input && result->data() && inputs[0]->data() && intersect_x1 > intersect_x0 && intersect_y1 > intersect_y0) {
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

    if (ctx.backend) {
        std::optional<raster::BBox> local_clip = has_echo ? std::nullopt : ctx.node_exec.clip_rect;
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
            .diagnostics_enabled = ctx.policy.diagnostics_enabled,
            .effect_processors = ctx.node_exec.current_effect_processors,
                .processor_snapshot = ctx.node_exec.processor_snapshot,
                .processors_resolved = ctx.node_exec.processor_bindings_compiled,
            .temporal_history_key = m_temporal_history_key
            , .second_input = inputs.size() > 1 ? inputs[1].get() : nullptr

        };
        // A producer may already have an authoritative native surface even
        // when strict native-GPU policy is disabled. Prefer that surface for
        // native effects too: its CPU mirror can be absent or stale (notably
        // for cropped MTSDF text), and copying the mirror would erase it.
        auto input_surface = inputs[0]->surface_handle();
        bool native_source_available =
            input_surface != runtime::kInvalidRenderSurfaceHandle &&
            ctx.backend->supports_native_surfaces() &&
            ctx.backend->is_native_surface_valid(input_surface);
        const auto native_clip = gpu_only_input
            ? std::optional<raster::BBox>{}
            : local_clip;
        EffectStack active_effects;
        for (const auto& effect : m_effects)
            if (effect.enabled) active_effects.push_back(effect);
        const auto echo_count = std::count_if(active_effects.begin(), active_effects.end(),
            [](const EffectInstance& effect) {
                return effect.effect_type == effects::EffectType::Echo;
            });
        bool native_echo_handled = false;
        if (has_echo && (gpu_only_input || ctx.policy.is_gpu_native_required()) &&
            active_effects.size() == 1 &&
            active_effects[0].effect_type == effects::EffectType::Echo) {
            native_echo_handled = native_effects::try_native_echo(
                ctx, active_effects[0], *inputs[0], *result,
                m_temporal_history_key, native_clip);
        }
        const bool strict_native_stack =
            (ctx.policy.is_gpu_native_required() || (gpu_only_input && has_echo)) &&
            active_effects.size() > 1 && ctx.backend->supports_native_surfaces() &&
            ctx.surface_registry && echo_count <= 1;
        bool native_source_promotion_failed = false;
        if ((strict_native_stack || ctx.policy.is_gpu_native_required()) &&
            !native_source_available && !gpu_only_input) {
            // Effect expansion may allocate a CPU-less output framebuffer under
            // strict GPU policy. Promote the actual input first, so spatial
            // stages can expand/crop that native source without trying to
            // upload an empty intermediate framebuffer.
            native_source_available = ensure_native_surface(ctx, *inputs[0]);
            input_surface = inputs[0]->surface_handle();
            native_source_promotion_failed = !native_source_available;
        }
        const auto native_source = native_source_available
            ? input_surface
            : runtime::kInvalidRenderSurfaceHandle;
        bool native_handled = false;
        // Strict GPU programs may compose several individually certified
        // effects. Each stage consumes the previous native surface and writes
        // a new one; the chain is published only after every active stage has
        // succeeded. This preserves authored order and never reads pixels back
        // between effects.
        if (native_echo_handled) {
            native_handled = true;
        } else if (strict_native_stack) {
            native_handled = !native_source_promotion_failed;
            for (const auto& effect : active_effects) {
                const auto* capability = gpu_effect_capability(effect.effect_type);
                if (!capability || capability->route != GpuEffectRoute::NativeZeroReadback) {
                    native_handled = false;
                    break;
                }
            }
            auto current_source = native_source;
            Framebuffer* current_source_framebuffer =
                native_source_available ? inputs[0].get() : nullptr;
            bool current_source_owned = false;
            for (const auto& effect : active_effects) {
                if (!native_handled) continue;
                EffectStack one_effect;
                one_effect.push_back(effect);
                bool stage_handled = false;
                switch (effect.effect_type) {
                case effects::EffectType::Blur:
                    stage_handled = native_effects::try_native_full_frame_blur(
                        ctx, one_effect, *result, native_clip, current_source,
                        current_source_framebuffer);
                    break;
                case effects::EffectType::Sharpen:
                    stage_handled = native_effects::try_native_full_frame_sharpen(
                        ctx, one_effect, *result, native_clip, current_source);
                    break;
                case effects::EffectType::Glow:
                case effects::EffectType::Bloom:
                    stage_handled = native_effects::try_native_full_frame_glow_or_bloom(
                        ctx, one_effect, *result, native_clip, current_source,
                        current_source_framebuffer);
                    break;
                case effects::EffectType::Tint:
                    stage_handled = native_effects::try_native_full_frame_tint(
                        ctx, one_effect, *result, local_clip, current_source);
                    break;
                case effects::EffectType::Echo:
                    stage_handled = native_effects::try_native_echo(
                        ctx, effect, *current_source_framebuffer, *result,
                        m_temporal_history_key, native_clip);
                    break;
                default:
                    stage_handled = native_effects::try_native_full_frame_color(
                        ctx, one_effect, *result, local_clip, current_source,
                        current_source_framebuffer);
                    break;
                }
                if (!stage_handled) {
                    native_handled = false;
                    break;
                }
                const auto previous_source = current_source;
                const auto next_source = result->surface_handle();
                if (current_source_owned && previous_source != next_source) {
                    (void)ctx.backend->release_surface(previous_source);
                    ctx.surface_registry->release(previous_source);
                }
                current_source = next_source;
                current_source_framebuffer = result.get();
                current_source_owned = true;
            }
            if (!native_handled && current_source_owned) {
                (void)ctx.backend->release_surface(current_source);
                ctx.surface_registry->release(current_source);
                result->clear_surface_handle();
            }
        } else if (native_source_promotion_failed &&
                   ctx.policy.is_gpu_native_required()) {
            // Never let a strict native route upload the CPU-less expanded
            // output after source promotion failed. Return the common
            // UnsupportedCapability result below instead of passing an empty
            // host span into a backend upload.
            native_handled = false;
        } else {
            native_handled = native_effects::try_native_full_frame_blur(
                ctx, active_effects, *result, local_clip,
                gpu_only_input ? inputs[0]->surface_handle()
                               : runtime::kInvalidRenderSurfaceHandle,
                gpu_only_input ? inputs[0].get() : nullptr);
            // The native glow entry point owns its capability guard. Unsupported
            // backends fall through to their registered software processor.
            if (!native_handled) {
                native_handled = native_effects::try_native_full_frame_sharpen(
                    ctx, active_effects, *result, native_clip, native_source);
            }            if (!native_handled) {
                native_handled = native_effects::try_native_full_frame_glow_or_bloom(
                    ctx, active_effects, *result, native_clip, native_source,
                    native_source_available ? inputs[0].get() : nullptr);
            }
            if (!native_handled && !gpu_only_input) {
                native_handled = native_effects::try_native_full_frame_tint(
                    ctx, active_effects, *result, local_clip);
            }
            if (!native_handled) {
                const auto color_source = native_source_available
                    ? native_source
                    : (gpu_only_input ? inputs[0]->surface_handle()
                                      : runtime::kInvalidRenderSurfaceHandle);
                native_handled = native_effects::try_native_full_frame_color(
                    ctx, active_effects, *result, local_clip, color_source,
                    color_source != runtime::kInvalidRenderSurfaceHandle ? inputs[0].get() : nullptr);
            }
            if (!native_handled) {
                const auto chromatic_source = native_source_available
                    ? native_source
                    : (gpu_only_input ? inputs[0]->surface_handle()
                                      : runtime::kInvalidRenderSurfaceHandle);
                native_handled = native_effects::try_native_full_frame_chromatic(
                    ctx, active_effects, *result, local_clip, chromatic_source,
                    chromatic_source != runtime::kInvalidRenderSurfaceHandle ? inputs[0].get() : nullptr);
            }
        }
        if ((ctx.policy.is_gpu_native_required() || (gpu_only_input && has_echo)) &&
            !native_handled) {
            std::string capability_message =
                "strict native GPU effect could not be executed; refusing software fallback";
            if (m_effects.size() == 1) {
                if (const auto* row = gpu_effect_capability(m_effects[0].effect_type))
                    capability_message = std::string(row->diagnostic) +
                        "; strict native GPU policy refuses software fallback";
            }
            return NodeExecResult{NodeExecutionError{
                RenderBackendErrorCode::UnsupportedCapability,
                "EffectStackNode",
                std::move(capability_message)}};
        }
        if (!native_handled && !gpu_only_input) {
            ctx.backend->apply_effect_stack(*result, m_effects, effect_context);
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
