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

// Native and software paths implement the same one-Gaussian Glow contract;
// this optimization has no quality-mode gate.
bool try_native_full_frame_glow(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip,
                                runtime::RenderSurfaceHandle existing_source,
                                const Framebuffer* existing_source_framebuffer) {
    if (!clip_covers_result(clip, result) || !ctx.backend ||
        !ctx.backend->supports_native_surfaces() || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::Glow) return false;
    const auto* params = std::get_if<GlowParams>(&instance.params);
    const bool valid_glow = params && std::isfinite(params->radius) &&
        std::isfinite(params->intensity) &&
        std::isfinite(params->color.r) && std::isfinite(params->color.g) &&
        std::isfinite(params->color.b) && std::isfinite(params->color.a) &&
        params->radius >= 0.0f && params->radius <= 64.0f &&
        params->intensity >= 0.0f;
    if (!valid_glow) {
        return false;
    }

    const auto desc = native_surface_desc(result.width(), result.height());
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
    const auto horizontal = ctx.surface_registry->create(desc);
    const auto vertical = ctx.surface_registry->create(desc);
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
        ? ctx.backend->create_surface(horizontal, desc)
        : upload_output;
    const auto create_vertical = create_horizontal.ok()
        ? ctx.backend->create_surface(vertical, desc)
        : create_horizontal;
    if (!create_vertical.ok()) {
        spdlog::error("[native-glow] surface setup failed: {}",
                      create_vertical.error().message);
        cleanup();
        return false;
    }

    // A single native Gaussian pass matches the software glow contract.
    const auto glow = ctx.backend->glow_surfaces(
        output, source, horizontal, vertical, params->radius,
        params->intensity, params->color);
    if (!glow.ok()) {
        spdlog::error("[native-glow] radius={} failed: {}",
                      params->radius, glow.error().message);
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
    {
        // TEMP-DIAG-GLOW: quantify source/output residency.
        const auto src_handle = (source_is_existing && !source_needs_expansion)
            ? existing_source : source;
        const auto ink = [&](runtime::RenderSurfaceHandle h) {
            auto scratch = ctx.download_scratch(result.width(), result.height());
            const auto res = ctx.backend->download_surface(h, scratch);
            std::uint64_t n = 0; const std::uint64_t t = scratch.size() / 4;
            for (std::size_t i = 0; i < t; ++i) if (scratch[i * 4 + 3] > 0.001f) ++n;
            return std::pair{res.ok(), n * 100 / (t ? t : 1)};
        };
        const auto src_stats = ink(src_handle);
        const auto out_stats = ink(output);
        spdlog::warn("[TEMP-DIAG-GLOW] src_ok={} src_ink={} out_ok={} out_ink={} existing={} expand={}",
                     src_stats.first, src_stats.second, out_stats.first, out_stats.second,
                     source_is_existing, source_needs_expansion);
    }
    result.set_surface_handle(output);
    return true;
}

bool try_native_full_frame_tint(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip) {
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
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
    // AUDIT F10 — zero-copy tight-storage path (see the glow fast-path above).
    std::vector<float> packed_rgba;
    const auto rgba = framebuffer_rgba_view(result, packed_rgba);
    const auto source = ctx.surface_registry->create(desc);
    const auto destination = ctx.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        if (source != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(source);
        if (destination != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(destination);
        return false;
    }
    const auto cleanup = [&] {
        (void)ctx.backend->release_surface(source);
        (void)ctx.backend->release_surface(destination);
        ctx.surface_registry->release(source);
        ctx.surface_registry->release(destination);
    };
    const auto created_source = ctx.backend->create_surface(source, desc);
    RenderOpResult uploaded = created_source;
    if (created_source.ok()) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        uploaded = ctx.backend->upload_surface(source, desc, rgba);
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
    (void)ctx.backend->release_surface(source);
    ctx.surface_registry->release(source);
    result.set_surface_handle(destination);
    return true;
}

bool try_native_full_frame_blur(RenderGraphContext& ctx,
                                const EffectStack& effect_stack,
                                Framebuffer& result,
                                const std::optional<raster::BBox>& clip) {
    if (!ctx.backend || !ctx.surface_registry ||
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
        : ctx.surface_registry->create(desc);
    const auto horizontal = ctx.surface_registry->create(desc);
    const auto output = ctx.surface_registry->create(desc);
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        horizontal == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        if (!source_is_existing && source != runtime::kInvalidRenderSurfaceHandle)
            ctx.surface_registry->release(source);
        if (horizontal != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(horizontal);
        if (output != runtime::kInvalidRenderSurfaceHandle) ctx.surface_registry->release(output);
        return false;
    }
    const auto cleanup = [&] {
        if (!source_is_existing) {
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
    if (source_ready.ok() && !source_is_existing) {
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
        local_clip = raster::BBox{
            clip->x0 - result.origin_x(), clip->y0 - result.origin_y(),
            clip->x1 - result.origin_x(), clip->y1 - result.origin_y()};
        local_clip->clip_to(result.width(), result.height());
    }
    const auto blurred_horizontal = created_output.ok()
        ? ctx.backend->blur_surface(horizontal, source, params->radius, /*horizontal=*/true)
        : created_output;
    const auto blurred_vertical = blurred_horizontal.ok()
        ? ctx.backend->blur_surface(output, horizontal, params->radius, /*horizontal=*/false)
        : blurred_horizontal;
    const auto clipped_result = blurred_vertical.ok() && partial_clip
        ? ctx.backend->copy_surface(source, output, local_clip)
        : blurred_vertical;
    if (!clipped_result.ok()) {
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
        (void)ctx.backend->release_surface(source);
        ctx.surface_registry->release(source);
        result.set_surface_handle(output);
    }
    return true;
}

} // namespace native_effects
