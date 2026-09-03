#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <chronon3d/runtime/gpu_memory_budget.hpp>
#include <cstdint>

namespace chronon3d::backends::vulkan {

enum class VulkanMemoryClass {
    DeviceLocal,
    HostUpload,
    HostReadback,
    ExternalExportable
};

struct VulkanBufferAllocation {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    void* mapped{nullptr};
    VkDeviceSize size{0};
};

struct VulkanImageAllocation {
    VkImage image{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkDeviceSize size{0};
    bool exportable{false};
};

struct VulkanMemoryBudgetStats {
    std::uint64_t allocation_bytes{0};
    std::uint64_t block_bytes{0};
    std::uint64_t allocation_count{0};
    std::uint64_t block_count{0};
    std::uint64_t budget_bytes{0};
    std::uint64_t usage_bytes{0};
};

class VulkanMemoryManager {
public:
    VulkanMemoryManager() = default;
    ~VulkanMemoryManager();

    VulkanMemoryManager(const VulkanMemoryManager&) = delete;
    VulkanMemoryManager& operator=(const VulkanMemoryManager&) = delete;

    void initialize(
        VkInstance instance,
        VkPhysicalDevice physical_device,
        VkDevice device);

    void shutdown();

    void set_budget_policy(runtime::GpuMemoryBudgetPolicy policy) noexcept {
        budget_resolver_.set_policy(policy);
    }

    [[nodiscard]] const runtime::GpuMemoryBudgetPolicy& budget_policy() const noexcept {
        return budget_resolver_.policy();
    }

    [[nodiscard]] runtime::GpuMemoryBudgetSnapshot budget_snapshot() const;

    [[nodiscard]]
    VulkanBufferAllocation create_buffer(
        const VkBufferCreateInfo& info,
        VulkanMemoryClass memory_class);

    void destroy_buffer(VulkanBufferAllocation& buffer);

    [[nodiscard]]
    VulkanImageAllocation create_image(
        const VkImageCreateInfo& info,
        VulkanMemoryClass memory_class);

    void destroy_image(VulkanImageAllocation& image);

    [[nodiscard]]
    VmaAllocationInfo allocation_info(
        VmaAllocation allocation) const;

    [[nodiscard]]
    VulkanMemoryBudgetStats budget_stats() const;

    [[nodiscard]]
    VmaAllocator native_allocator() const noexcept {
        return allocator_;
    }

private:
    void require_reservation(std::uint64_t bytes, const char* resource_kind) const;

    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    runtime::GpuMemoryBudgetResolver budget_resolver_{};
};

} // namespace chronon3d::backends::vulkan
#endif
