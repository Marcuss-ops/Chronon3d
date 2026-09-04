#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include "memory/vulkan_memory_manager.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace chronon3d::backends::vulkan {

// Sole owner of staging and rotating asynchronous upload state.
struct VulkanUploadAuthority {
    struct UploadSlot {
        VulkanBufferAllocation buffer_allocation{};
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        std::uint64_t ticket{0};
        bool in_flight{false};
    };

    static constexpr std::size_t kSlotCount = 3;
    VulkanBufferAllocation staging{};
    std::array<UploadSlot, kSlotCount> slots{};
    std::size_t next_slot{0};
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
