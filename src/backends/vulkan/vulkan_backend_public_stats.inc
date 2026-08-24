// Vulkan public capabilities and telemetry accessors. Included by the backend TU to preserve the
// single public implementation while keeping each responsibility inspectable.
graph::RenderCapabilities VulkanBackend::capabilities() const noexcept {
    return graph::RenderCapabilities{
        // TextRunNode first attempts the native GlyphAtlas/RenderSurface
        // path. Capability must describe that native entry point, not the
        // removed software compatibility bridge.
        .text_run = true};
}

std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
VulkanBackend::processor_snapshot() const noexcept {
    return m_draw_node_fallback ? m_draw_node_fallback->processor_snapshot() : nullptr;
}

bool VulkanBackend::requires_processor_snapshot() const noexcept {
    return m_draw_node_fallback && m_draw_node_fallback->requires_processor_snapshot();
}

VulkanBackendStats VulkanBackend::stats() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return VulkanBackendStats{};
    auto s = m_impl->stats;
    const auto vma_stats = m_impl->memory_manager.budget_stats();
    s.vma_allocation_bytes = vma_stats.allocation_bytes;
    s.vma_block_bytes = vma_stats.block_bytes;
    s.vma_allocation_count = vma_stats.allocation_count;
    s.vma_block_count = vma_stats.block_count;
    s.vma_budget_bytes = vma_stats.budget_bytes;
    s.vma_usage_bytes = vma_stats.usage_bytes;
    return s;
#else
    return {};
#endif
}

void VulkanBackend::record_fallback_counter(
    const char* reason, std::uint64_t elapsed_us) noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return;
    ++m_impl->stats.software_fallback_nodes;
    if (std::strcmp(reason, "draw_node") == 0) ++m_impl->stats.fallback_draw_node;
    else if (std::strcmp(reason, "text_run") == 0) ++m_impl->stats.fallback_text_run;
    else if (std::strcmp(reason, "composite") == 0) ++m_impl->stats.fallback_composite;
    else if (std::strcmp(reason, "effect") == 0) ++m_impl->stats.fallback_effect;
    else if (std::strcmp(reason, "blur") == 0) ++m_impl->stats.fallback_blur;
    else if (std::strcmp(reason, "dof") == 0) ++m_impl->stats.fallback_dof;
    m_impl->stats.software_fallback_us += elapsed_us;
#else
    (void)reason;
    (void)elapsed_us;
#endif
}

void VulkanBackend::record_fallback_shape(bool image) noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return;
    if (image) ++m_impl->stats.fallback_draw_image;
    else ++m_impl->stats.fallback_draw_other;
#else
    (void)image;
#endif
}

void VulkanBackend::record_fallback_composite(bool dimensions) noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return;
    if (dimensions) ++m_impl->stats.fallback_composite_dimensions;
    else ++m_impl->stats.fallback_composite_mode;
#else
    (void)dimensions;
#endif
}

void VulkanBackend::composite_legacy_surface(
    Framebuffer& destination, const Framebuffer& source, BlendMode mode,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    m_impl->composite(destination.surface_handle(), source.surface_handle(), mode, clip);
#else
    (void)destination;
    (void)source;
    (void)mode;
    (void)clip;
    unsupported("composite_layer");
#endif
}

void VulkanBackend::export_gpu_telemetry_counters(
    std::vector<std::pair<std::string, std::uint64_t>>& out) const {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return;
    out.emplace_back("gpu_submissions", m_impl->stats.submissions);
    out.emplace_back("passes_executed", m_impl->stats.passes_executed);
    out.emplace_back("gpu_upload_count", m_impl->stats.upload_calls);
    out.emplace_back("gpu_upload_bytes", m_impl->stats.upload_bytes);
    out.emplace_back("gpu_upload_full_surface_bytes", m_impl->stats.upload_full_surface_bytes);
    out.emplace_back("gpu_upload_region_bytes", m_impl->stats.upload_region_bytes);
    static constexpr std::array<std::pair<const char*, profiling::GpuUploadProducer>, 8> producers{{
        {"unknown", profiling::GpuUploadProducer::Unknown},
        {"video", profiling::GpuUploadProducer::Video},
        {"projection", profiling::GpuUploadProducer::Projection},
        {"composition", profiling::GpuUploadProducer::Composition},
        {"text", profiling::GpuUploadProducer::Text},
        {"effects", profiling::GpuUploadProducer::Effects},
        {"image", profiling::GpuUploadProducer::Image},
        {"other", profiling::GpuUploadProducer::Other},
    }};
    for (const auto& [name, producer] : producers) {
        const auto index = static_cast<std::size_t>(producer);
        out.emplace_back(std::string("gpu_upload_") + name + "_bytes",
                         m_impl->stats.upload_producer_bytes[index]);
        out.emplace_back(std::string("gpu_upload_") + name + "_full_count",
                         m_impl->stats.upload_producer_full_count[index]);
        out.emplace_back(std::string("gpu_upload_") + name + "_region_count",
                         m_impl->stats.upload_producer_region_count[index]);
    }
    out.emplace_back("gpu_readback_bytes", m_impl->stats.readback_bytes);
    out.emplace_back("physical_surfaces_peak", m_impl->stats.physical_surfaces_peak);
    out.emplace_back("gpu_submit_cpu_us", m_impl->stats.gpu_submit_cpu_us);
    out.emplace_back("gpu_wait_cpu_us", m_impl->stats.gpu_wait_cpu_us);
    out.emplace_back("standalone_wait_count", m_impl->stats.standalone_wait_count);
    out.emplace_back("standalone_wait_us", m_impl->stats.standalone_wait_us);
    out.emplace_back("frame_batch_drain_wait_count", m_impl->stats.frame_batch_drain_wait_count);
    out.emplace_back("frame_batch_drain_wait_us", m_impl->stats.frame_batch_drain_wait_us);
    out.emplace_back("frame_slot_wait_count", m_impl->stats.frame_slot_wait_count);
    out.emplace_back("frame_slot_wait_us", m_impl->stats.frame_slot_wait_us);
    out.emplace_back("readback_us", m_impl->stats.readback_us);
    out.emplace_back("cpu_gpu_sync_us", m_impl->stats.gpu_wait_cpu_us + m_impl->stats.readback_us);
    out.emplace_back("gpu_execute_us", m_impl->stats.gpu_execute_us);
    out.emplace_back("gpu_nodes", m_impl->stats.passes_executed);
    out.emplace_back("software_fallback_nodes", m_impl->stats.software_fallback_nodes);
    out.emplace_back("software_fallback_us", m_impl->stats.software_fallback_us);
    out.emplace_back("fallback_draw_node", m_impl->stats.fallback_draw_node);
    out.emplace_back("fallback_draw_image", m_impl->stats.fallback_draw_image);
    out.emplace_back("fallback_draw_other", m_impl->stats.fallback_draw_other);
    out.emplace_back("fallback_text_run", m_impl->stats.fallback_text_run);
    out.emplace_back("fallback_composite", m_impl->stats.fallback_composite);
    out.emplace_back("fallback_composite_dimensions", m_impl->stats.fallback_composite_dimensions);
    out.emplace_back("fallback_composite_mode", m_impl->stats.fallback_composite_mode);
    out.emplace_back("fallback_effect", m_impl->stats.fallback_effect);
    out.emplace_back("fallback_blur", m_impl->stats.fallback_blur);
    out.emplace_back("fallback_dof", m_impl->stats.fallback_dof);
    out.emplace_back("layer_batch_calls", m_impl->stats.layer_batch_calls);
    out.emplace_back("layer_instances", m_impl->stats.layer_instances_processed);
    out.emplace_back("legacy_transform_calls", m_impl->stats.legacy_transform_calls);
    out.emplace_back("legacy_composite_calls", m_impl->stats.legacy_composite_calls);
    out.emplace_back("text_batch_calls", m_impl->stats.text_batch_calls);
    out.emplace_back("glyphs", m_impl->stats.glyphs_processed);
    out.emplace_back("legacy_text_run_surface_calls", m_impl->stats.legacy_text_run_surface_calls);
    out.emplace_back("vkCmdDispatch", m_impl->stats.vk_cmd_dispatch_count);
    out.emplace_back("vkCmdDraw", m_impl->stats.vk_cmd_draw_count);
    out.emplace_back("descriptor_allocations", m_impl->stats.descriptor_allocations);
    out.emplace_back("barriers", m_impl->stats.barriers_emitted);
    const auto vma_stats = m_impl->memory_manager.budget_stats();
    out.emplace_back("vma_allocation_bytes", vma_stats.allocation_bytes);
    out.emplace_back("vma_block_bytes", vma_stats.block_bytes);
    out.emplace_back("vma_allocation_count", vma_stats.allocation_count);
    out.emplace_back("vma_block_count", vma_stats.block_count);
    out.emplace_back("vma_budget_bytes", vma_stats.budget_bytes);
    out.emplace_back("vma_usage_bytes", vma_stats.usage_bytes);
#else
    (void)out;
#endif
}

std::size_t VulkanBackend::physical_surface_count() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return m_impl ? m_impl->physical_surfaces.size() : 0;
#else
    return 0;
#endif
}

const GpuKernelRegistry& VulkanBackend::kernel_registry() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    static const GpuKernelRegistry empty{};
    return m_impl ? m_impl->kernel_registry : empty;
#else
    static const GpuKernelRegistry empty{};
    return empty;
#endif
}
