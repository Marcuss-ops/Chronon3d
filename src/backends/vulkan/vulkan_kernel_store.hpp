#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/gpu_kernel_registry.hpp>
#include <vulkan/vulkan.h>

// vulkan_backend_lifecycle_private.cpp persists the cache with
// std::filesystem while constructing this owner. Keep that dependency
// available from the private Vulkan kernel/cache boundary so a clean TU build
// does not depend on unrelated transitive standard-library includes.
#include <filesystem>

namespace chronon3d::backends::vulkan {

// Private owner of Vulkan compute pipelines, their shared pipeline cache, and
// their layouts. GpuKernelRegistry remains the single authority for the
// registered semantic pipeline set; VkPipelineCache is only the Vulkan driver
// cache used while creating those registered pipelines.
class VulkanKernelStore {
public:
    GpuKernelRegistry registry{};
    VkPipelineCache pipeline_cache{VK_NULL_HANDLE};
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

        if (pipeline_cache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device, pipeline_cache, nullptr);
            pipeline_cache = VK_NULL_HANDLE;
        }

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
