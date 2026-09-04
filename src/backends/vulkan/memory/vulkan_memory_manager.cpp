#ifdef CHRONON3D_ENABLE_VULKAN
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "vulkan_memory_manager.hpp"
#include <chronon3d/internal/testing/failure_injector.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::backends::vulkan {
namespace {

std::uint64_t saturating_mul(std::uint64_t a, std::uint64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    if (a > std::numeric_limits<std::uint64_t>::max() / b) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return a * b;
}

std::uint64_t format_bytes_per_texel(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SFLOAT:
            return 2;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R32_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
            return 2; // conservative upper bound for 1.5 B/px NV12
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
            return 4; // conservative upper bound for P010
        default:
            // Chronon's render allocator predominantly uses the formats above.
            // Unknown formats take a conservative bound rather than silently
            // bypassing the hard policy.
            return 16;
    }
}

std::uint64_t estimate_image_bytes(const VkImageCreateInfo& info) noexcept {
    std::uint64_t total = 0;
    std::uint64_t width = std::max<std::uint32_t>(1, info.extent.width);
    std::uint64_t height = std::max<std::uint32_t>(1, info.extent.height);
    std::uint64_t depth = std::max<std::uint32_t>(1, info.extent.depth);
    const std::uint64_t bpp = format_bytes_per_texel(info.format);
    const std::uint64_t layers = std::max<std::uint32_t>(1, info.arrayLayers);
    const std::uint64_t samples = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(info.samples));
    const std::uint32_t levels = std::max<std::uint32_t>(1, info.mipLevels);

    for (std::uint32_t level = 0; level < levels; ++level) {
        std::uint64_t level_bytes = saturating_mul(width, height);
        level_bytes = saturating_mul(level_bytes, depth);
        level_bytes = saturating_mul(level_bytes, layers);
        level_bytes = saturating_mul(level_bytes, samples);
        level_bytes = saturating_mul(level_bytes, bpp);
        if (total > std::numeric_limits<std::uint64_t>::max() - level_bytes) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += level_bytes;
        width = std::max<std::uint64_t>(1, width >> 1);
        height = std::max<std::uint64_t>(1, height >> 1);
        depth = std::max<std::uint64_t>(1, depth >> 1);
    }
    return total;
}

bool consumes_device_budget(VulkanMemoryClass memory_class) noexcept {
    return memory_class == VulkanMemoryClass::DeviceLocal ||
           memory_class == VulkanMemoryClass::ExternalExportable;
}

} // namespace

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

runtime::GpuMemoryBudgetSnapshot VulkanMemoryManager::budget_snapshot() const {
    const auto native = budget_stats();
    return budget_resolver_.resolve(
        native.usage_bytes, native.budget_bytes,
        native.allocation_bytes, native.block_bytes);
}

void VulkanMemoryManager::require_reservation(
    std::uint64_t bytes, const char* resource_kind) const {
    const auto snapshot = budget_snapshot();
    if (budget_resolver_.can_reserve(snapshot, bytes)) return;
    throw std::runtime_error(
        std::string{"VulkanMemoryManager: GPU memory hard budget rejects "} +
        resource_kind + " allocation bytes=" + std::to_string(bytes) +
        " usage=" + std::to_string(snapshot.usage_bytes) +
        " hard_limit=" + std::to_string(snapshot.hard_limit_bytes));
}

VulkanBufferAllocation VulkanMemoryManager::create_buffer(
    const VkBufferCreateInfo& buffer_info,
    VulkanMemoryClass memory_class)
{
    if (chronon3d::testing::FailureInjector::should_fail(
            chronon3d::testing::FailurePoint::VulkanBufferAllocation)) {
        throw std::runtime_error("VulkanMemoryManager: injected buffer allocation failure");
    }
    if (allocator_ == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanMemoryManager: create_buffer called on uninitialized allocator");
    }
    if (consumes_device_budget(memory_class)) {
        require_reservation(static_cast<std::uint64_t>(buffer_info.size), "buffer");
    }

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    switch (memory_class) {
        case VulkanMemoryClass::DeviceLocal:
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case VulkanMemoryClass::HostUpload:
            // UploadRing writes directly through the persistent mapping. Make
            // coherency part of the memory-class contract instead of assuming
            // the VMA-selected HOST_VISIBLE type also happens to be coherent.
            // With HOST_COHERENT guaranteed, no vmaFlushAllocation is needed.
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case VulkanMemoryClass::HostReadback:
            // Readback copies into a persistently mapped buffer and reads it
            // immediately after the GPU fence. HOST_COHERENT makes the
            // invalidate requirement explicit by construction: none is needed.
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case VulkanMemoryClass::ExternalExportable:
            alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
    }

    VulkanBufferAllocation result{};
    VmaAllocationInfo result_info{};

    const VkResult vk_result = vmaCreateBuffer(
        allocator_, &buffer_info, &alloc_info,
        &result.buffer, &result.allocation, &result_info);

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
    if (consumes_device_budget(memory_class)) {
        require_reservation(estimate_image_bytes(image_info), "image");
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
        allocator_, &image_info, &alloc_info,
        &result.image, &result.allocation, &result_info);

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
        if (props.memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            stats.budget_bytes += budgets[i].budget;
            stats.usage_bytes += budgets[i].usage;
        }
    }

    return stats;
}

} // namespace chronon3d::backends::vulkan
#endif