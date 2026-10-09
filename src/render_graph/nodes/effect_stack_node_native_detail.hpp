namespace native_effects {

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

// Native Glow/Bloom use caller-planned full-frame surfaces and scratch images.
bool try_native_full_frame_glow_or_bloom(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip,
                                runtime::RenderSurfaceHandle existing_source,
                                const Framebuffer* existing_source_framebuffer) {
    // DropShadow's software oracle intentionally ignores the executor clip:
    // EffectStackNode has already expanded the input ROI for the halo.
    (void)clip;
    if (!ctx.backend || !ctx.backend->supports_native_surfaces() ||
        !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled) return false;
    const auto* glow_params = std::get_if<GlowParams>(&instance.params);
    const auto* bloom_params = std::get_if<BloomParams>(&instance.params);
    if ((instance.effect_type != effects::EffectType::Glow || !glow_params) &&
        (instance.effect_type != effects::EffectType::Bloom || !bloom_params))
        return false;
    const float radius = glow_params ? glow_params->radius : bloom_params->radius;
    const float intensity = glow_params ? glow_params->intensity : bloom_params->intensity;
    const bool valid_params = std::isfinite(radius) && std::isfinite(intensity) &&
        radius >= 0.0f && radius <= 96.0f && intensity >= 0.0f && intensity <= 4.0f &&
        (!bloom_params || (std::isfinite(bloom_params->threshold) &&
                           bloom_params->threshold >= 0.0f && bloom_params->threshold <= 1.0f &&
                           radius <= 10.0f)) &&
        (!glow_params || (std::isfinite(glow_params->color.r) &&
                          std::isfinite(glow_params->color.g) &&
                          std::isfinite(glow_params->color.b) &&
                          std::isfinite(glow_params->color.a)));
    if (!valid_params) {
        return false;
    }

    const auto desc = native_surface_desc(result.width(), result.height());
    // Preserve the software halo oracle at every authored radius. The device
    // loss was premature surface retirement, not full-resolution filtering;
    // downsampling here changed the approved effect rather than fixing it.
    const auto scratch_desc = desc;
    // AUDIT F10 — zero-copy when the framebuffer storage is tightly packed (the
    // common case for graph-owned "result" buffers): only a stride-padded
    // framebuffer now pays a full-frame pack. The uploader copies into its
    // staging buffer before returning, so the view only needs to outlive this
    // call. This removed a ~33 MB allocate+copy per effect per frame at 1080p
    // (RGBA-f32), which is exactly the steady-state heap churn the
    // check_steady_state_allocations gate is meant to catch.
    const bool source_is_existing =
        existing_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_needs_expansion = source_is_existing &&
        existing_source_framebuffer &&
        (existing_source_framebuffer->width() != result.width() ||
         existing_source_framebuffer->height() != result.height() ||
         existing_source_framebuffer->origin_x() != result.origin_x() ||
         existing_source_framebuffer->origin_y() != result.origin_y());
    std::vector<float> packed_rgba;
    const auto rgba = source_is_existing
        ? std::span<const float>{}
        : framebuffer_rgba_view(result, packed_rgba);
    const auto source = source_is_existing && !source_needs_expansion
        ? existing_source
        : ctx.surface_registry->create(desc);
    const auto output = ctx.surface_registry->create(desc);
    const auto horizontal = ctx.surface_registry->create(scratch_desc);
    const auto vertical = ctx.surface_registry->create(scratch_desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle ||
        horizontal == runtime::kInvalidRenderSurfaceHandle ||
        vertical == runtime::kInvalidRenderSurfaceHandle) {
        if ((!source_is_existing || source_needs_expansion) &&
            source != runtime::kInvalidRenderSurfaceHandle)
            ctx.surface_registry->release(source);
        if (output != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(output);
        if (horizontal != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(horizontal);
        if (vertical != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(vertical);
        return false;
    }
    const auto cleanup = [&] {
        if (!source_is_existing || source_needs_expansion)
            (void)ctx.backend->release_surface(source);
        (void)ctx.backend->release_surface(output);
        (void)ctx.backend->release_surface(horizontal);
        (void)ctx.backend->release_surface(vertical);
        if (!source_is_existing || source_needs_expansion)
            ctx.surface_registry->release(source);
        ctx.surface_registry->release(output);
        ctx.surface_registry->release(horizontal);
        ctx.surface_registry->release(vertical);
    };
    const auto source_ready = source_is_existing && !source_needs_expansion
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.backend->create_surface(source, desc);
    RenderOpResult upload = source_ready;
    if (source_needs_expansion && source_ready.ok()) {
        upload = ctx.backend->initialize_transparent_surface(source);
        if (upload.ok()) {
            const int offset_x = existing_source_framebuffer->origin_x() - result.origin_x();
            const int offset_y = existing_source_framebuffer->origin_y() - result.origin_y();
            upload = ctx.backend->transform_surface(
                source, existing_source, offset_x, offset_y, 1.0f);
        }
    } else if (!source_is_existing && source_ready.ok()) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        upload = ctx.backend->upload_surface(source, desc, rgba);
    }
    const auto create_output = upload.ok()
        ? ctx.backend->create_surface(output, desc)
        : upload;
    RenderOpResult upload_output = create_output;
    if (create_output.ok()) {
        if (source_is_existing) {
            upload_output = ctx.backend->copy_surface(output, source);
        } else {
            profiling::GpuUploadProducerScope upload_scope(
                profiling::GpuUploadProducer::Effects);
            upload_output = ctx.backend->upload_surface(output, desc, rgba);
        }
    }
    const auto create_horizontal = upload_output.ok()
        ? ctx.backend->create_surface(horizontal, scratch_desc)
        : upload_output;
    const auto create_vertical = create_horizontal.ok()
        ? ctx.backend->create_surface(vertical, scratch_desc)
        : create_horizontal;
    if (!create_vertical.ok()) {
        spdlog::error("[native-glow] surface setup failed: {}",
                      create_vertical.error().message);
        cleanup();
        return false;
    }

    const auto native_effect = glow_params
        ? ctx.backend->glow_surfaces(output, source, horizontal, vertical,
                                     radius, intensity, glow_params->color)
        : ctx.backend->bloom_surfaces(output, source, horizontal, vertical,
                                      bloom_params->threshold, radius, intensity);
    if (!native_effect.ok()) {
        spdlog::error("[native-effect] radius={} failed: {}",
                      radius, native_effect.error().message);
        cleanup();
        return false;
    }

    if (!source_is_existing || source_needs_expansion)
        (void)ctx.backend->release_surface(source);
    (void)ctx.backend->release_surface(horizontal);
    (void)ctx.backend->release_surface(vertical);
    if (!source_is_existing || source_needs_expansion)
        ctx.surface_registry->release(source);
    ctx.surface_registry->release(horizontal);
    ctx.surface_registry->release(vertical);
    result.set_surface_handle(output);
    return true;
}

// Compatibility entry point used by the adjustment-node implementation.
bool try_native_full_frame_glow(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip,
                                runtime::RenderSurfaceHandle existing_source,
                                const Framebuffer* existing_source_framebuffer) {
    if (effect_stack.size() != 1 ||
        effect_stack[0].effect_type != effects::EffectType::Glow) {
        return false;
    }
    return try_native_full_frame_glow_or_bloom(
        ctx, effect_stack, result, clip, existing_source,
        existing_source_framebuffer);
}

// Native drop shadow keeps the two authored alpha silhouettes on Vulkan:
// offset + blur the contact and ambient maps, combine them, then composite the
// original source over the shadow. Radius 32 is the current image-motion
// catalog maximum; its ambient lobe reaches 64 pixels.
bool try_native_full_frame_drop_shadow(
    RenderGraphContext& ctx, const EffectStack& effect_stack,
    Framebuffer& result, const std::optional<raster::BBox>& clip,
    runtime::RenderSurfaceHandle borrowed_source,
    const Framebuffer* borrowed_source_framebuffer = nullptr) {
    (void)clip;
    if (!ctx.backend || !ctx.backend->supports_native_surfaces() || !ctx.surface_registry ||
        effect_stack.size() != 1) return false;
    const auto& instance = effect_stack[0];
    const auto* params = std::get_if<DropShadowParams>(&instance.params);
    if (!instance.enabled || instance.effect_type != effects::EffectType::DropShadow ||
        !params || !std::isfinite(params->radius) || params->radius < 0.0f ||
        params->radius > 32.0f || !std::isfinite(params->offset.x) ||
        !std::isfinite(params->offset.y) || std::abs(params->offset.x) > 100000.0f ||
        std::abs(params->offset.y) > 100000.0f ||
        !std::isfinite(params->color.r) || !std::isfinite(params->color.g) ||
        !std::isfinite(params->color.b) || !std::isfinite(params->color.a) ||
        params->color.a < 0.0f || params->color.a > 1.0f) return false;

    const auto desc = native_surface_desc(result.width(), result.height());
    const bool source_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_needs_expansion = source_borrowed && borrowed_source_framebuffer &&
        (borrowed_source_framebuffer->width() != result.width() ||
         borrowed_source_framebuffer->height() != result.height() ||
         borrowed_source_framebuffer->origin_x() != result.origin_x() ||
         borrowed_source_framebuffer->origin_y() != result.origin_y());
    auto source = source_borrowed ? borrowed_source : result.surface_handle();
    if (source == runtime::kInvalidRenderSurfaceHandle) return false;
    if (source_needs_expansion) {
        const auto expanded = ctx.surface_registry->create(desc);
        if (expanded == runtime::kInvalidRenderSurfaceHandle ||
            !ctx.backend->create_surface(expanded, desc).ok()) {
            if (expanded != runtime::kInvalidRenderSurfaceHandle)
                ctx.surface_registry->release(expanded);
            return false;
        }
        auto expand_op = ctx.backend->initialize_transparent_surface(expanded);
        if (expand_op.ok()) {
            const int offset_x = borrowed_source_framebuffer->origin_x() - result.origin_x();
            const int offset_y = borrowed_source_framebuffer->origin_y() - result.origin_y();
            expand_op = ctx.backend->transform_surface(expanded, source, offset_x, offset_y, 1.0f);
        }
        if (!expand_op.ok()) {
            (void)ctx.backend->release_surface(expanded);
            ctx.surface_registry->release(expanded);
            return false;
        }
        source = expanded;
    }

    std::array<runtime::RenderSurfaceHandle, 9> scratch{};
    for (auto& handle : scratch) {
        handle = ctx.surface_registry->create(desc);
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            for (auto allocated : scratch) {
                if (allocated != runtime::kInvalidRenderSurfaceHandle)
                    ctx.surface_registry->release(allocated);
            }
            return false;
        }
    }
    const auto [solid, mask, contact_shift, contact_h, contact_v,
                ambient_shift, ambient_h, ambient_v, output] = scratch;
    const auto cleanup = [&] {
        if (source_needs_expansion) {
            (void)ctx.backend->release_surface(source);
            ctx.surface_registry->release(source);
        }
        for (auto handle : scratch) {
            (void)ctx.backend->release_surface(handle);
            ctx.surface_registry->release(handle);
        }
    };
    for (auto handle : scratch) {
        if (!ctx.backend->create_surface(handle, desc).ok()) {
            cleanup();
            return false;
        }
    }

    const float contact_radius = std::max(1.0f, params->radius * 0.45f);
    const float ambient_radius = std::max(contact_radius + 4.0f,
                                           params->radius * 2.0f);
    const int ox = static_cast<int>(std::round(params->offset.x));
    const int oy = static_cast<int>(std::round(params->offset.y));
    const int ambient_x = static_cast<int>(std::round(static_cast<float>(ox) * 1.65f));
    const int ambient_y = static_cast<int>(std::round(static_cast<float>(oy) * 1.65f));
    const chronon3d::Color premultiplied(
        params->color.r * params->color.a,
        params->color.g * params->color.a,
        params->color.b * params->color.a,
        params->color.a);
    auto op = ctx.backend->fill_rect_surface(
        solid, 0, 0, result.width(), result.height(), premultiplied);
    if (op.ok()) op = ctx.backend->matte_surface(mask, solid, source, false, false);
    if (op.ok()) op = ctx.backend->initialize_transparent_surface(contact_shift);
    if (op.ok()) op = ctx.backend->transform_surface(
        contact_shift, mask, ox, oy, 0.85f);
    if (op.ok()) op = ctx.backend->blur_surface(
        contact_h, contact_shift, contact_radius, true, true);
    if (op.ok()) op = ctx.backend->blur_surface(
        contact_v, contact_h, contact_radius, false, true);
    if (op.ok()) op = ctx.backend->initialize_transparent_surface(ambient_shift);
    if (op.ok()) op = ctx.backend->transform_surface(
        ambient_shift, mask, ambient_x, ambient_y, 0.30f);
    if (op.ok()) op = ctx.backend->blur_surface(
        ambient_h, ambient_shift, ambient_radius, true, true);
    if (op.ok()) op = ctx.backend->blur_surface(
        ambient_v, ambient_h, ambient_radius, false, true);
    // Both lobes use the same straight color; source-over combines their
    // coverage without touching or reading pixels back to the host.
    if (op.ok()) op = ctx.backend->composite_surfaces(
        ambient_v, contact_v, BlendMode::Normal, CompositeOperator::SourceOver);
    if (op.ok()) op = ctx.backend->copy_surface(output, ambient_v);
    if (op.ok()) op = ctx.backend->composite_surfaces(
        output, source, BlendMode::Normal, CompositeOperator::SourceOver);
    if (!op.ok()) {
        spdlog::error("[native-drop-shadow] Vulkan pass failed: {}",
                      op.error().message);
        cleanup();
        return false;
    }
    for (auto handle : scratch) {
        if (handle == output) continue;
        (void)ctx.backend->release_surface(handle);
        ctx.surface_registry->release(handle);
    }
    result.set_surface_handle(output);
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

    const auto desc = native_surface_desc(result.width(), result.height());
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
        if (!source_is_existing) (void)ctx.backend->release_surface(source);
        (void)ctx.backend->release_surface(destination);
        if (!source_is_existing) ctx.surface_registry->release(source);
        ctx.surface_registry->release(destination);
    };
    auto created_source = source_is_existing
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.backend->create_surface(source, desc);
    RenderOpResult uploaded = created_source;
    if (created_source.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        uploaded = ctx.backend->upload_surface(
            source, desc, framebuffer_rgba_view(result, packed_rgba));
    }
    const auto created_destination = uploaded.ok()
        ? ctx.backend->create_surface(destination, desc)
        : uploaded;
    const auto adjusted = created_destination.ok()
        ? ctx.backend->color_adjust_surface(
            destination, source, 0.0f, 1.0f, params->color,
            params->color.a * params->amount)
        : created_destination;
    if (!adjusted.ok()) {
        cleanup();
        return false;
    }
    if (!source_is_borrowed) {
        (void)ctx.backend->release_surface(source);
        ctx.surface_registry->release(source);
    }
    result.set_surface_handle(destination);
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
    const auto desc = native_surface_desc(result.width(), result.height());
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
            (void)ctx.backend->release_surface(source);
            ctx.surface_registry->release(source);
        }
        (void)ctx.backend->release_surface(horizontal);
        (void)ctx.backend->release_surface(output);
        ctx.surface_registry->release(horizontal);
        ctx.surface_registry->release(output);
    };
    const auto source_ready = source_is_existing
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.backend->create_surface(source, desc);
    RenderOpResult uploaded = source_ready;
    if (source_ready.ok() && source_needs_expansion) {
        uploaded = ctx.backend->initialize_transparent_surface(source);
        if (uploaded.ok()) {
            const int offset_x = borrowed_source_framebuffer->origin_x() - result.origin_x();
            const int offset_y = borrowed_source_framebuffer->origin_y() - result.origin_y();
            uploaded = ctx.backend->transform_surface(source, borrowed_source, offset_x, offset_y, 1.0f);
        }
    } else if (source_ready.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        // AUDIT F10 — zero-copy tight-storage path (see the glow fast-path).
        std::vector<float> packed_rgba;
        uploaded = ctx.backend->upload_surface(
            source, desc, framebuffer_rgba_view(result, packed_rgba));
    }
    const auto created_horizontal = uploaded.ok()
        ? ctx.backend->create_surface(horizontal, desc)
        : uploaded;
    const auto created_output = created_horizontal.ok()
        ? ctx.backend->create_surface(output, desc)
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
    const auto pass1_horizontal = created_output.ok()
        ? ctx.backend->blur_surface(horizontal, source, static_cast<float>(box_radius),
                                    /*horizontal=*/true, /*box_kernel=*/true)
        : created_output;
    const auto pass1_vertical = pass1_horizontal.ok()
        ? ctx.backend->blur_surface(output, horizontal, static_cast<float>(box_radius),
                                    /*horizontal=*/false, /*box_kernel=*/true)
        : pass1_horizontal;
    const auto pass2_horizontal = pass1_vertical.ok()
        ? ctx.backend->blur_surface(
            horizontal, output, static_cast<float>(box_radius),
            /*horizontal=*/true, /*box_kernel=*/true)
        : pass1_vertical;
    const auto blurred_vertical = pass2_horizontal.ok()
        ? ctx.backend->blur_surface(
            output, horizontal, static_cast<float>(box_radius),
            /*horizontal=*/false, /*box_kernel=*/true)
        : pass2_horizontal;
    const auto clipped_result = blurred_vertical.ok() && partial_clip
        ? ctx.backend->copy_surface(source, output, local_clip)
        : blurred_vertical;
    if (!clipped_result.ok()) {
        spdlog::error("[native-blur] radius={} derived_box_radius={} failed: {}",
                      params->radius, box_radius, clipped_result.error().message);
        cleanup();
        return false;
    }
    (void)ctx.backend->release_surface(horizontal);
    ctx.surface_registry->release(horizontal);
    if (partial_clip) {
        (void)ctx.backend->release_surface(output);
        ctx.surface_registry->release(output);
        result.set_surface_handle(source);
    } else {
        if (!source_is_borrowed || source_needs_expansion) {
            (void)ctx.backend->release_surface(source);
            ctx.surface_registry->release(source);
        }
        result.set_surface_handle(output);
    }
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
                                   runtime::RenderSurfaceHandle borrowed_source) {
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
    const auto desc = native_surface_desc(result.width(), result.height());
    const bool has_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool has_result_surface =
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend->is_native_surface_valid(result.surface_handle());
    if (!has_borrowed && !has_result_surface) {
        return false;
    }
    const auto source = has_borrowed ? borrowed_source : result.surface_handle();
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
        return false;
    }
    const auto release_scratch = [&] {
        (void)ctx.backend->release_surface(scratch_a);
        (void)ctx.backend->release_surface(scratch_b);
        (void)ctx.backend->release_surface(blurred);
        ctx.surface_registry->release(scratch_a);
        ctx.surface_registry->release(scratch_b);
        ctx.surface_registry->release(blurred);
    };
    const int rounded = std::max(1, static_cast<int>(std::round(params->radius)));
    const double r_plus = static_cast<double>(rounded) * rounded + rounded;
    const int box_radius = std::max(1, static_cast<int>(std::round(
        0.5 * (std::sqrt(1.0 + 6.0 * r_plus) - 1.0))));
    const float box = static_cast<float>(box_radius);
    const auto created = ctx.backend->create_surface(scratch_a, desc).ok()
        ? ctx.backend->create_surface(scratch_b, desc)
        : RenderOpResult(RenderOpOutcome{});
    const auto ready = created.ok() ? ctx.backend->create_surface(blurred, desc) : created;
    const auto dst_ready = ready.ok() ? ctx.backend->create_surface(destination, desc) : ready;
    if (!dst_ready.ok()) {
        release_scratch();
        ctx.surface_registry->release(destination);
        return false;
    }
    const auto blurred_ok = ctx.backend->blur_surface(scratch_a, source, box, true, true).ok()
        ? ctx.backend->blur_surface(scratch_b, scratch_a, box, false, true)
        : RenderOpResult(RenderOpOutcome{});
    const auto blurred_done = blurred_ok.ok()
        ? ctx.backend->blur_surface(scratch_a, scratch_b, box, true, true)
        : blurred_ok;
    const auto blurred_final = blurred_done.ok()
        ? ctx.backend->blur_surface(blurred, scratch_a, box, false, true)
        : blurred_done;
    const auto combined = blurred_final.ok()
        ? ctx.backend->sharpen_combine_surface(destination, source, blurred,
                                               params->amount, params->threshold)
        : blurred_final;
    if (!combined.ok()) {
        release_scratch();
        (void)ctx.backend->release_surface(destination);
        ctx.surface_registry->release(destination);
        return false;
    }
    release_scratch();
    result.set_surface_handle(destination);
    return true;
}

} // namespace native_effects
