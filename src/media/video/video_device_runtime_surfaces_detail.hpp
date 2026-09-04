namespace chronon3d::media {

bool VideoDeviceRuntime::acquire_slot_surfaces(
    runtime::FrameExecutionSlot& slot,
    runtime::RenderSurfaceRegistry& registry,
    graph::RenderBackend& backend,
    std::uint32_t width,
    std::uint32_t height,
    std::string& reason) {
    reason.clear();
    if (width == 0 || height == 0) {
        reason = "invalid native video surface dimensions";
        return false;
    }
    if (!backend.supports_native_video_surface() ||
        !backend.supports_native_surfaces()) {
        reason = "backend does not support native video surfaces";
        return false;
    }
    if (!slot.transition_interop_state(runtime::InteropFrameState::Allocated)) {
        reason = "execution slot is not recyclable";
        return false;
    }

    const auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::PipelineSlot);

    // A slot owns its two PipelineSlot surfaces across frames.  Releasing the
    // lease returns the slot to Recyclable; it must not force a new Vulkan
    // allocation on the next acquisition.  Validate the retained bindings and
    // reuse them, which keeps physical surface count bounded by the ring.
    if (slot.native_surface != runtime::kInvalidRenderSurfaceHandle ||
        slot.source_surface != runtime::kInvalidRenderSurfaceHandle) {
        const auto* encode_desc = registry.lookup(slot.native_surface);
        const auto* source_desc = registry.lookup(slot.source_surface);
        const bool reusable = encode_desc && source_desc &&
            encode_desc->desc.width == desc.width &&
            encode_desc->desc.height == desc.height &&
            encode_desc->desc.format == desc.format &&
            source_desc->desc.width == desc.width &&
            source_desc->desc.height == desc.height &&
            source_desc->desc.format == desc.format &&
            backend.is_native_surface_valid(slot.native_surface) &&
            backend.is_native_surface_valid(slot.source_surface);
        if (reusable) {
            slot.backend = &backend;
            return true;
        }
        reason = "recyclable slot has stale or incompatible native surfaces";
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        return false;
    }

    const auto encode = registry.create(desc);
    const auto source = registry.create(desc);
    if (encode == runtime::kInvalidRenderSurfaceHandle ||
        source == runtime::kInvalidRenderSurfaceHandle) {
        if (encode != runtime::kInvalidRenderSurfaceHandle) registry.release(encode);
        if (source != runtime::kInvalidRenderSurfaceHandle) registry.release(source);
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        reason = "surface registry rejected native video surface";
        return false;
    }

    const auto encode_result = backend.create_video_encode_surface(encode, desc);
    if (!encode_result.ok()) {
        registry.release(encode);
        registry.release(source);
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        reason = encode_result.error().message;
        return false;
    }
    const auto source_result = backend.create_surface(source, desc);
    if (!source_result.ok()) {
        (void)backend.release_surface(encode);
        registry.release(encode);
        registry.release(source);
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        reason = source_result.error().message;
        return false;
    }

    slot.backend = &backend;
    slot.native_surface = encode;
    slot.source_surface = source;
    return true;
}

} // namespace chronon3d::media
