#ifdef CHRONON3D_ENABLE_VULKAN
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "vulkan_memory_manager.hpp"
#include <chronon3d/internal/testing/failure_injector.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::backends::vulkan {

VulkanMemoryManager::~VulkanMemoryManager() {
    shutdown();
}

void VulkanMemoryManager::initialize(
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device)
{
    physical_device_ = physical_device;
    device_ = device;

    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &props);

    std::vector<VkExternalMemoryHandleTypeFlags> external_types(props.memoryTypeCount, 0);
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            external_types[i] = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        }
    }
#endif

    VmaAllocatorCreateInfo info{};
    info.instance = instance;
    info.physicalDevice = physical_device;
    info.device = device;
    info.vulkanApiVersion = VK_API_VERSION_1_2;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    info.pTypeExternalMemoryHandleTypes = external_types.data();
#endif

    const VkResult result = vmaCreateAllocator(&info, &allocator_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            "VulkanMemoryManager: vmaCreateAllocator failed with VkResult=" +
            std::to_string(static_cast<int>(result)));
    }
}

void VulkanMemoryManager::shutdown() {
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
    physical_device_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

VulkanBufferAllocation VulkanMemoryManager::create_buffer(
    const VkBufferCreateInfo& buffer_info,
    VulkanMemoryClass memory_class)
{
    if (allocator_ == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanMemoryManager: create_buffer called on uninitialized allocator");
    }

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    switch (memory_class) {
        case VulkanMemoryClass::DeviceLocal:
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case VulkanMemoryClass::HostUpload:
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case VulkanMemoryClass::HostReadback:
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case VulkanMemoryClass::ExternalExportable:
            alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
    }

    VulkanBufferAllocation result{};
    VmaAllocationInfo result_info{};

    const VkResult vk_result = vmaCreateBuffer(
        allocator_,
        &buffer_info,
        &alloc_info,
        &result.buffer,
        &result.allocation,
        &result_info);

    if (vk_result != VK_SUCCESS) {
        throw std::runtime_error(
            "VulkanMemoryManager: vmaCreateBuffer failed with VkResult=" +
            std::to_string(static_cast<int>(vk_result)));
    }

    result.mapped = result_info.pMappedData;
    result.size = result_info.size;
    return result;
}

void VulkanMemoryManager::destroy_buffer(VulkanBufferAllocation& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

VulkanImageAllocation VulkanMemoryManager::create_image(
    const VkImageCreateInfo& image_info,
    VulkanMemoryClass memory_class)
{
    if (chronon3d::testing::FailureInjector::should_fail(
            chronon3d::testing::FailurePoint::VulkanImageAllocation)) {
        throw std::runtime_error("VulkanMemoryManager: injected image allocation failure");
    }
    if (allocator_ == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanMemoryManager: create_image called on uninitialized allocator");
    }

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    if (memory_class == VulkanMemoryClass::DeviceLocal) {
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else if (memory_class == VulkanMemoryClass::ExternalExportable) {
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    VulkanImageAllocation result{};
    VmaAllocationInfo result_info{};

    const VkResult vk_result = vmaCreateImage(
        allocator_,
        &image_info,
        &alloc_info,
        &result.image,
        &result.allocation,
        &result_info);

    if (vk_result != VK_SUCCESS) {
        throw std::runtime_error(
            "VulkanMemoryManager: vmaCreateImage failed with VkResult=" +
            std::to_string(static_cast<int>(vk_result)));
    }

    if (memory_class == VulkanMemoryClass::ExternalExportable && result_info.offset != 0) {
        vmaDestroyImage(allocator_, result.image, result.allocation);
        throw std::runtime_error("External Vulkan allocation must be dedicated (offset == 0)");
    }

    result.size = result_info.size;
    result.exportable = (memory_class == VulkanMemoryClass::ExternalExportable);
    return result;
}

void VulkanMemoryManager::destroy_image(VulkanImageAllocation& image) {
    if (image.image != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image.image, image.allocation);
    }
    image = {};
}

VmaAllocationInfo VulkanMemoryManager::allocation_info(VmaAllocation allocation) const {
    VmaAllocationInfo info{};
    if (allocator_ != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
        vmaGetAllocationInfo(allocator_, allocation, &info);
    }
    return info;
}

VulkanMemoryBudgetStats VulkanMemoryManager::budget_stats() const {
    VulkanMemoryBudgetStats stats{};
    if (allocator_ == VK_NULL_HANDLE || physical_device_ == VK_NULL_HANDLE) return stats;

    VmaTotalStatistics total_stats{};
    vmaCalculateStatistics(allocator_, &total_stats);
    stats.allocation_bytes = total_stats.total.statistics.allocationBytes;
    stats.block_bytes = total_stats.total.statistics.blockBytes;
    stats.allocation_count = total_stats.total.statistics.allocationCount;
    stats.block_count = total_stats.total.statistics.blockCount;

    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);
    std::vector<VmaBudget> budgets(props.memoryHeapCount);
    vmaGetHeapBudgets(allocator_, budgets.data());
    for (std::uint32_t i = 0; i < props.memoryHeapCount; ++i) {
        if (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            stats.budget_bytes += budgets[i].budget;
            stats.usage_bytes += budgets[i].usage;
        }
    }

    return stats;
}

} // namespace chronon3d::backends::vulkan
#endif
