#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::backends::vulkan {

// Shared Vulkan result boundary is defined by vulkan_backend_impl.hpp in every
// translation unit that instantiates this private allocator.
inline void check(VkResult result, const char* operation);

// Owns one geometrically growing family of resettable descriptor pools.
// Allocation policy lives here so callers cannot create ad-hoc frame pools.
class FrameDescriptorAllocator {
public:
    static constexpr std::size_t kInitialChunkSets = 64;
    static constexpr std::size_t kStorageImagesPerSet = 3;
    static constexpr std::size_t kStorageBuffersPerSet = 2;

    void create(VkDevice device, VkDescriptorSetLayout layout,
                std::size_t storage_images = kStorageImagesPerSet,
                std::size_t storage_buffers = kStorageBuffersPerSet) {
        device_ = device;
        layout_ = layout;
        storage_images_ = storage_images;
        storage_buffers_ = storage_buffers;
    }

    void destroy() noexcept {
        for (auto pool : pools_) {
            if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, pool, nullptr);
        }
        pools_.clear();
        active_pool_ = 0;
    }

    void reset() {
        for (auto pool : pools_) {
            check(vkResetDescriptorPool(device_, pool, 0),
                  "vkResetDescriptorPool(frame descriptor chunk)");
        }
        active_pool_ = 0;
    }

    [[nodiscard]] VkDescriptorSet allocate() {
        if (active_pool_ >= pools_.size()) grow();
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
            pools_[active_pool_], 1, &layout_};
        const VkResult result = vkAllocateDescriptorSets(device_, &allocation, &set);
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY ||
            result == VK_ERROR_FRAGMENTED_POOL) {
            ++active_pool_;
            return allocate();
        }
        check(result, "vkAllocateDescriptorSets(frame descriptor chunk)");
        return set;
    }

private:
    void grow() {
        const std::size_t sets = kInitialChunkSets * (std::size_t{1} << pools_.size());
        std::vector<VkDescriptorPoolSize> pool_sizes;
        if (storage_images_ != 0) {
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                  static_cast<std::uint32_t>(sets * storage_images_)});
        }
        if (storage_buffers_ != 0) {
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  static_cast<std::uint32_t>(sets * storage_buffers_)});
        }
        const VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            static_cast<std::uint32_t>(sets),
            static_cast<std::uint32_t>(pool_sizes.size()), pool_sizes.data()};
        VkDescriptorPool pool = VK_NULL_HANDLE;
        check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool),
              "vkCreateDescriptorPool(frame descriptor chunk)");
        pools_.push_back(pool);
    }

    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
    std::size_t storage_images_{kStorageImagesPerSet};
    std::size_t storage_buffers_{kStorageBuffersPerSet};
    std::vector<VkDescriptorPool> pools_{};
    std::size_t active_pool_{0};
};

// Sole owner of descriptor-related Vulkan handles and per-frame allocators.
// VulkanBackend::Impl may expose references for migration compatibility, but
// it must not own a second descriptor pool/set/layout state.
struct VulkanDescriptorAuthority {
    std::array<FrameDescriptorAllocator, 3> pass{};
    std::array<FrameDescriptorAllocator, 3> text_tile_bin{};
    std::array<FrameDescriptorAllocator, 3> text_tile_raster{};

    VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorSetLayout text_tile_bin_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorSetLayout text_tile_raster_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, 3> glow_descriptor_sets{};

    void destroy() noexcept {
        for (auto& allocator : pass) allocator.destroy();
        for (auto& allocator : text_tile_bin) allocator.destroy();
        for (auto& allocator : text_tile_raster) allocator.destroy();
    }

    void reset(std::size_t slot) {
        pass[slot].reset();
        text_tile_bin[slot].reset();
        text_tile_raster[slot].reset();
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
