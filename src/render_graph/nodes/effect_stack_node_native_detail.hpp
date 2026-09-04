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
    RenderOpResult upload = create_output;
    if (create_output.ok()) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        upload = ctx.services.backend->upload_surface(output, desc, rgba);
    }
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
    RenderOpResult uploaded = created_source;
    if (created_source.ok()) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        uploaded = ctx.services.backend->upload_surface(source, desc, rgba);
    }
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
    if (!ctx.services.backend || !ctx.services.surface_registry ||
        effect_stack.size() != 1) {
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
    const bool source_is_existing =
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle;
    const auto source = source_is_existing
        ? result.surface_handle()
        : ctx.services.surface_registry->create(desc);
    const auto horizontal = ctx.services.surface_registry->create(desc);
    const auto output = ctx.services.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        horizontal == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        if (!source_is_existing && source != runtime::kInvalidRenderSurfaceHandle)
            ctx.services.surface_registry->release(source);
        if (horizontal != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(horizontal);
        if (output != runtime::kInvalidRenderSurfaceHandle) ctx.services.surface_registry->release(output);
        return false;
    }
    const auto cleanup = [&] {
        if (!source_is_existing) {
            (void)ctx.services.backend->release_surface(source);
            ctx.services.surface_registry->release(source);
        }
        (void)ctx.services.backend->release_surface(horizontal);
        (void)ctx.services.backend->release_surface(output);
        ctx.services.surface_registry->release(horizontal);
        ctx.services.surface_registry->release(output);
    };
    const auto source_ready = source_is_existing
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.services.backend->create_surface(source, desc);
    RenderOpResult uploaded = source_ready;
    if (source_ready.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        uploaded = ctx.services.backend->upload_surface(
            source, desc, pack_framebuffer_rgba(result));
    }
    const auto created_horizontal = uploaded.ok()
        ? ctx.services.backend->create_surface(horizontal, desc)
        : uploaded;
    const auto created_output = created_horizontal.ok()
        ? ctx.services.backend->create_surface(output, desc)
        : created_horizontal;
    const bool partial_clip = clip && !clip_covers_result(clip, result);
    std::optional<raster::BBox> local_clip;
    if (partial_clip) {
        local_clip = raster::BBox{
            clip->x0 - result.origin_x(), clip->y0 - result.origin_y(),
            clip->x1 - result.origin_x(), clip->y1 - result.origin_y()};
        local_clip->clip_to(result.width(), result.height());
    }
    const auto blurred_horizontal = created_output.ok()
        ? ctx.services.backend->blur_surface(horizontal, source, params->radius, /*horizontal=*/true)
        : created_output;
    const auto blurred_vertical = blurred_horizontal.ok()
        ? ctx.services.backend->blur_surface(output, horizontal, params->radius, /*horizontal=*/false)
        : blurred_horizontal;
    const auto clipped_result = blurred_vertical.ok() && partial_clip
        ? ctx.services.backend->copy_surface(source, output, local_clip)
        : blurred_vertical;
    if (!clipped_result.ok()) {
        cleanup();
        return false;
    }
    (void)ctx.services.backend->release_surface(horizontal);
    ctx.services.surface_registry->release(horizontal);
    if (partial_clip) {
        (void)ctx.services.backend->release_surface(output);
        ctx.services.surface_registry->release(output);
        result.set_surface_handle(source);
    } else {
        (void)ctx.services.backend->release_surface(source);
        ctx.services.surface_registry->release(source);
        result.set_surface_handle(output);
    }
    return true;
}

} // namespace native_effects
