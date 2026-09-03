// vulkan_backend_stats.cpp — VulkanBackend public capabilities and telemetry
// accessors (capabilities(), stats(), export_gpu_telemetry_counters(), …).
// Split out of vulkan_backend.cpp so touching the stats surface recompiles
// only this TU, never the surface/op adapters or the kernel store.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

graph::RenderCapabilities VulkanBackend::capabilities() const noexcept {
    return graph::RenderCapabilities{
        // TextRunNode first attempts the native GlyphAtlas/RenderSurface
        // path. Capability must describe that native entry point, not the
        // removed software compatibility bridge.
        .text_run = true};
}

std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
VulkanBackend::processor_snapshot() const noexcept {
    return nullptr;
}

bool VulkanBackend::requires_processor_snapshot() const noexcept {
    return false;
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
    s.physical_surfaces_live = m_impl->surfaces.physical_count();
    s.surface_bindings_live = m_impl->surfaces.binding_count();
    s.deferred_surface_release_count = m_impl->surfaces.deferred_release_count();
    s.command_batch_active = m_impl->command_batch_active;
    s.command_batch_started = m_impl->command_batch_started;
    return s;
#else
    return {};
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
    out.emplace_back("gpu_asset_cache_hits", m_impl->stats.gpu_asset_cache_hits);
    out.emplace_back("gpu_asset_cache_misses", m_impl->stats.gpu_asset_cache_misses);
    out.emplace_back("gpu_asset_cache_initial_uploads", m_impl->stats.gpu_asset_cache_initial_uploads);
    out.emplace_back("gpu_asset_cache_initial_upload_bytes", m_impl->stats.gpu_asset_cache_initial_upload_bytes);
    out.emplace_back("gpu_asset_cache_evictions", m_impl->stats.gpu_asset_cache_evictions);
    out.emplace_back("gpu_asset_cache_evicted_bytes", m_impl->stats.gpu_asset_cache_evicted_bytes);
    out.emplace_back("gpu_asset_cache_resident_bytes", m_impl->stats.gpu_asset_cache_resident_bytes);
    out.emplace_back("physical_surfaces_peak", m_impl->stats.physical_surfaces_peak);
    out.emplace_back("physical_surfaces_live", m_impl->surfaces.physical_count());
    out.emplace_back("surface_bindings_live", m_impl->surfaces.binding_count());
    out.emplace_back("deferred_surface_release_count", m_impl->surfaces.deferred_release_count());
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
    out.emplace_back("layer_batch_calls", m_impl->stats.layer_batch_calls);
    out.emplace_back("layer_instances", m_impl->stats.layer_instances_processed);
    out.emplace_back("text_batch_calls", m_impl->stats.text_batch_calls);
    out.emplace_back("glyphs", m_impl->stats.glyphs_processed);
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
    return m_impl ? m_impl->surface_physical_count() : 0;
#else
    return 0;
#endif
}

const GpuKernelRegistry& VulkanBackend::kernel_registry() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    static const GpuKernelRegistry empty{};
    return m_impl ? m_impl->kernels.registry : empty;
#else
    static const GpuKernelRegistry empty{};
    return empty;
#endif
}

} // namespace chronon3d::backends::vulkan
