#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/gpu_kernel_registry.hpp>
#include <vulkan/vulkan.h>

namespace chronon3d::backends::vulkan {

// Private owner of Vulkan compute pipelines and their layouts.
// GpuKernelRegistry remains the single authority for the registered pipeline
// set: destruction enumerates the registry itself rather than maintaining a
// second hard-coded GpuKernelId list.
class VulkanKernelStore {
public:
    GpuKernelRegistry registry{};
    VkPipelineLayout general_layout{VK_NULL_HANDLE};
    VkPipelineLayout text_tile_bin_layout{VK_NULL_HANDLE};
    VkPipelineLayout text_tile_raster_layout{VK_NULL_HANDLE};

    void destroy(VkDevice device) noexcept {
        registry.for_each_registered(
            [device](GpuKernelId, GpuKernelRegistry::PipelineHandle handle) {
                if (handle != 0) {
                    vkDestroyPipeline(
                        device, reinterpret_cast<VkPipeline>(handle), nullptr);
                }
            });
        registry = {};

        if (general_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, general_layout, nullptr);
        }
        if (text_tile_bin_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, text_tile_bin_layout, nullptr);
        }
        if (text_tile_raster_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, text_tile_raster_layout, nullptr);
        }
        general_layout = VK_NULL_HANDLE;
        text_tile_bin_layout = VK_NULL_HANDLE;
        text_tile_raster_layout = VK_NULL_HANDLE;
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
