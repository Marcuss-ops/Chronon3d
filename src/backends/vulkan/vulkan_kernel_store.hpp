#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/gpu_kernel_registry.hpp>
#include <vulkan/vulkan.h>

// vulkan_backend_lifecycle_private.cpp persists the cache with
// std::filesystem while constructing this owner. Keep that dependency
// available from the private Vulkan kernel/cache boundary so a clean TU build
// does not depend on unrelated transitive standard-library includes.
#include <array>
#include <cstddef>
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
    // inline_mesh.comp declares binding 4 as a read-only storage image
    // (metallic-roughness). The general layout binds a storage buffer there,
    // so the inline mesh owns this dedicated layout.
    VkPipelineLayout inline_mesh_layout{VK_NULL_HANDLE};
    VkPipelineLayout inline_mesh_bin_layout{VK_NULL_HANDLE};
    VkPipelineLayout particle_emitter_layout{VK_NULL_HANDLE};
    VkPipelineLayout text_tile_bin_layout{VK_NULL_HANDLE};
    VkPipelineLayout text_tile_raster_layout{VK_NULL_HANDLE};

    static constexpr std::size_t kColorMatrixSpecModeCount = 19;
    static constexpr std::size_t kWarpBulkSpecModeCount = 13;
    std::array<VkPipeline, kColorMatrixSpecModeCount> color_matrix_mode_pipelines{};
    std::array<VkPipeline, kWarpBulkSpecModeCount> warp_bulk_mode_pipelines{};

    void destroy(VkDevice device) noexcept {
        for (VkPipeline& p : color_matrix_mode_pipelines) {
            if (p != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, p, nullptr);
                p = VK_NULL_HANDLE;
            }
        }
        for (VkPipeline& p : warp_bulk_mode_pipelines) {
            if (p != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, p, nullptr);
                p = VK_NULL_HANDLE;
            }
        }
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
        if (inline_mesh_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, inline_mesh_layout, nullptr);
        }
        if (inline_mesh_bin_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, inline_mesh_bin_layout, nullptr);
        }
        if (particle_emitter_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, particle_emitter_layout, nullptr);
        }
        if (text_tile_bin_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, text_tile_bin_layout, nullptr);
        }
        if (text_tile_raster_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, text_tile_raster_layout, nullptr);
        }
        general_layout = VK_NULL_HANDLE;
        inline_mesh_layout = VK_NULL_HANDLE;
        inline_mesh_bin_layout = VK_NULL_HANDLE;
        particle_emitter_layout = VK_NULL_HANDLE;
        text_tile_bin_layout = VK_NULL_HANDLE;
        text_tile_raster_layout = VK_NULL_HANDLE;
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
