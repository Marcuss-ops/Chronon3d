// Vulkan public surface and operation adapters. Included by the backend TU to preserve the
// single public implementation while keeping each responsibility inspectable.
graph::RenderOpResult VulkanBackend::create_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        (void)m_impl->ensure_surface(handle, desc);
        // Generic graph surfaces are transient even when created while a
        // batch is recording. Only CUDA/NVENC external surfaces are unplanned;
        // marking every in-batch surface unplanned prevents reclamation.
        ++m_impl->stats.surface_creations;
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput, error.what()});
    }
#else
    (void)handle; (void)desc;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::create_surface: Vulkan support is disabled"});
#endif
}

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
graph::RenderOpResult VulkanBackend::create_cuda_external_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->create_cuda_external_surface(handle, desc);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
}

graph::RenderOpResult VulkanBackend::create_video_encode_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
    return create_cuda_external_surface(handle, desc);
}

graph::RenderOpResult VulkanBackend::copy_surface_to_video_encode(
    runtime::RenderSurfaceHandle source, runtime::RenderSurfaceHandle destination) {
    return copy_surface_to_cuda_encoder(source, destination);
}

graph::RenderOpResult VulkanBackend::copy_surface_to_cuda_encoder(
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination,
    bool wait_for_completion) {
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->copy_surface_to_cuda_encoder(source, destination, wait_for_completion);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
}

graph::RenderOpResult VulkanBackend::prepare_cuda_surface_for_vulkan(
    runtime::RenderSurfaceHandle handle) {
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->prepare_cuda_surface_for_vulkan(handle);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
}

CudaExternalMemoryInfo VulkanBackend::export_cuda_external_memory(
    runtime::RenderSurfaceHandle handle) const {
    std::lock_guard lock(m_impl->api_mutex);
    return m_impl->export_cuda_external_memory(handle);
}
#endif

graph::RenderOpResult VulkanBackend::release_surface(
    runtime::RenderSurfaceHandle handle) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::release_surface: invalid handle"});
        }
        // A command buffer may still reference any surface created during the
        // active batch.  "Unplanned" only means that the barrier planner does
        // not know the handle; it must never shorten the Vulkan lifetime.
        if (m_impl->frame_batch.active || m_impl->command_batch_active) {
            if (std::find(m_impl->deferred_surface_releases.begin(),
                          m_impl->deferred_surface_releases.end(), handle) ==
                m_impl->deferred_surface_releases.end()) {
                m_impl->deferred_surface_releases.push_back(handle);
            }
            return graph::RenderOpResult(graph::RenderOpOutcome{});
        }
        m_impl->release_surface_now(handle);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::release_surface: Vulkan support is disabled"});
#endif
}

bool VulkanBackend::is_native_surface_valid(
    runtime::RenderSurfaceHandle handle) const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl || handle == runtime::kInvalidRenderSurfaceHandle) return false;
    std::lock_guard lock(m_impl->api_mutex);
    return m_impl->surface_bindings.contains(handle);
#else
    (void)handle;
    return false;
#endif
}

void VulkanBackend::release_frame_transient_surfaces() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (m_impl) {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->release_frame_transient_surfaces();
    }
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::span<const float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        (void)m_impl->upload(handle, desc, rgba, true);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)desc; (void)rgba;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface_region(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height,
    std::span<const float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        (void)m_impl->upload_region(handle, desc, x, y, width, height, rgba, true);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)desc; (void)x; (void)y; (void)width; (void)height; (void)rgba;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface_region: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface_async(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::span<const float> rgba, runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        ticket.value = m_impl->upload(handle, desc, rgba, false);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        ticket = {};
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)desc; (void)rgba; (void)ticket;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface_async: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::wait_upload(const runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        if (!ticket.valid()) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::wait_upload: invalid ticket"});
        }
        if (ticket.value > m_impl->next_timeline_value) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::wait_upload: unknown ticket"});
        }
        m_impl->wait_upload_ticket(ticket.value);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)ticket;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::wait_upload: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::download_surface(
    runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->download(handle, rgba);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)rgba;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::download_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::composite_surfaces(
    runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
    BlendMode mode, CompositeOperator op,
    const std::optional<raster::BBox>& clip) {
    if ((mode != BlendMode::Normal && mode != BlendMode::Add) ||
        op != CompositeOperator::SourceOver) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "VulkanBackend::composite_surfaces: only Normal/Add SourceOver is implemented"});
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->composite(destination, source, mode, clip);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::composite_surfaces: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::copy_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->composite(destination, source, BlendMode::Normal, clip, true);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)clip;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::copy_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::fill_rect_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Color& color) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->fill_rect(destination, x0, y0, x1, y1, color);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_rect_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    int offset_x, int offset_y, float opacity) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->transform(destination, source, offset_x, offset_y, opacity);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)offset_x; (void)offset_y; (void)opacity;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->transform_affine(destination, source, transform);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)transform;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface_affine: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::blur_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source, float radius, bool horizontal) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->blur(destination, source, radius, horizontal);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)radius; (void)horizontal;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::blur_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::glow_surfaces(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    float radius, float intensity, const Color& tint) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->glow(destination, source, scratch_horizontal, scratch_vertical,
                     radius, intensity, tint);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)scratch_horizontal;
    (void)scratch_vertical; (void)radius; (void)intensity; (void)tint;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::glow_surfaces: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::color_adjust_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float brightness, float contrast, const Color& tint, float tint_amount) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->color_adjust(destination, source, brightness, contrast, tint, tint_amount);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)brightness; (void)contrast;
    (void)tint; (void)tint_amount;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::color_adjust_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::matte_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle target,
    runtime::RenderSurfaceHandle matte,
    bool luma, bool inverted) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->matte(destination, target, matte, luma, inverted);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)target; (void)matte; (void)luma; (void)inverted;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::matte_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->text_run_surface(destination, atlas, glyphs);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)atlas; (void)glyphs;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run_surface_timed(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->api_mutex);
        m_impl->text_run_surface(destination, atlas, glyphs, current_frame,
                                 highlight_color, highlight_enabled);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)atlas; (void)glyphs; (void)current_frame;
    (void)highlight_color; (void)highlight_enabled;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface_timed: Vulkan support is disabled"});
#endif
}

std::unique_ptr<graph::RenderBackend> make_vulkan_backend() {
