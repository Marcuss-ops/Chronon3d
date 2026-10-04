// Included inside namespace chronon3d::graph by effect_stack_node.cpp.
// Native Echo lowers to frame-owned GPU surfaces and weighted source-over
// composites. History is addressed by exact frame number through JobSession.
namespace native_effects {

inline bool try_native_echo(
    RenderGraphContext& ctx, const EffectInstance& effect,
    Framebuffer& source, Framebuffer& result, std::string_view history_key,
    const std::optional<raster::BBox>& clip) {
    const auto* params = std::get_if<EchoParams>(&effect.params);
    if (!params || params->echoes == 0 || params->echoes > 8 ||
        !std::isfinite(params->decay) || params->decay < 0.0f ||
        params->decay > 1.0f || params->time_offset_frames == 0 ||
        params->time_offset_frames > 120 || history_key.empty() ||
        !ctx.execution || !ctx.backend || !ctx.surface_registry ||
        !ctx.backend->supports_native_surfaces() ||
        ctx.frame_input.width <= 0 || ctx.frame_input.height <= 0 ||
        result.width() != ctx.frame_input.width ||
        result.height() != ctx.frame_input.height) {
        return false;
    }

    auto& job = ctx.execution->job();
    if (source.surface_handle() == runtime::kInvalidRenderSurfaceHandle &&
        !ensure_native_surface(ctx, source, "Echo current-frame source")) {
        return false;
    }
    const auto source_handle = source.surface_handle();
    if (!ctx.backend->is_native_surface_valid(source_handle)) return false;

    const auto width = ctx.frame_input.width;
    const auto height = ctx.frame_input.height;
    const auto desc = native_surface_desc(width, height);
    std::vector<runtime::RenderSurfaceHandle> temporaries;
    const auto create_surface = [&]() -> runtime::RenderSurfaceHandle {
        const auto handle = ctx.surface_registry->create(desc);
        if (handle == runtime::kInvalidRenderSurfaceHandle) return handle;
        const auto created = ctx.backend->create_surface(handle, desc);
        if (!created.ok()) {
            (void)ctx.backend->release_surface(handle);
            (void)ctx.surface_registry->release(handle);
            return runtime::kInvalidRenderSurfaceHandle;
        }
        temporaries.push_back(handle);
        return handle;
    };
    const auto release_temporaries = [&] {
        for (const auto handle : temporaries) {
            (void)ctx.backend->release_surface(handle);
            (void)ctx.surface_registry->release(handle);
        }
        temporaries.clear();
    };
    const auto run = [&](RenderOpResult operation) {
        return operation.ok();
    };

    const auto current = create_surface();
    const auto trail = create_surface();
    const auto composed = create_surface();
    const auto output = create_surface();
    if (current == runtime::kInvalidRenderSurfaceHandle ||
        trail == runtime::kInvalidRenderSurfaceHandle ||
        composed == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        release_temporaries();
        return false;
    }

    bool succeeded = run(ctx.backend->initialize_transparent_surface(current)) &&
        run(ctx.backend->initialize_transparent_surface(trail)) &&
        run(ctx.backend->initialize_transparent_surface(composed)) &&
        run(ctx.backend->initialize_transparent_surface(output));
    if (succeeded) {
        succeeded = run(ctx.backend->transform_surface(
            current, source_handle, source.origin_x(), source.origin_y(), 1.0f));
    }
    if (!succeeded) {
        release_temporaries();
        return false;
    }

    const auto current_frame = ctx.frame_input.frame.integral();
    const auto history = job.native_echo_frames(
        history_key, current_frame, params->echoes, params->time_offset_frames);
    const auto keep_frames = static_cast<std::size_t>(params->echoes) *
        static_cast<std::size_t>(params->time_offset_frames) + 1U;
    if (!job.record_native_echo_frame(
            history_key, current_frame, current, keep_frames,
            *ctx.backend, *ctx.surface_registry)) {
        release_temporaries();
        return false;
    }

    // Match the software order: oldest and dimmest first, then nearer echoes,
    // then the current source as the topmost image.
    for (std::uint32_t i = params->echoes; i > 0 && succeeded; --i) {
        const auto& sample = history[i - 1];
        if (!sample || sample->desc.width != static_cast<std::uint32_t>(width) ||
            sample->desc.height != static_cast<std::uint32_t>(height)) continue;
        const float weight = std::pow(params->decay, static_cast<float>(i));
        succeeded = run(ctx.backend->composite_surface_weighted(
            trail, sample->handle, weight, clip));
    }
    if (succeeded)
        succeeded = run(ctx.backend->composite_surfaces(
            composed, trail, BlendMode::Normal, CompositeOperator::SourceOver));
    if (succeeded)
        succeeded = run(ctx.backend->composite_surfaces(
            composed, current, BlendMode::Normal, CompositeOperator::SourceOver));
    if (succeeded)
        succeeded = run(ctx.backend->copy_surface(output, current));
    if (succeeded)
        succeeded = run(ctx.backend->copy_surface(output, composed, clip));
    if (!succeeded) {
        release_temporaries();
        return false;
    }

    // Transfer the output handle to the result framebuffer. It is frame
    // transient and follows the normal native framebuffer lifetime path.
    temporaries.erase(std::remove(temporaries.begin(), temporaries.end(), output),
                      temporaries.end());
    result.set_surface_handle(output);
    result.mark_gpu_authoritative();
    result.set_origin(0, 0);
    result.set_opaque(false);
    result.set_key_digest(source.key_digest());
    release_temporaries();
    return true;
}

} // namespace native_effects
