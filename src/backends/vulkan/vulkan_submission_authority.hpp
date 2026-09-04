#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include "vulkan_descriptor_authority.hpp"
#include "memory/vulkan_memory_manager.hpp"

#include <chronon3d/runtime/command_plan.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::backends::vulkan {

// Owns every Vulkan submission-lifetime primitive: rotating frame slots,
// replay slots and the immediate/timeline submission state. Descriptor
// allocators are borrowed from the descriptor authority rather than copied.
struct VulkanSubmissionAuthority {
    static constexpr std::size_t kSlotCount = 3;
    static constexpr std::size_t kCompiledPassTimingCapacity = 256;

    bool active{false};
    std::size_t next_slot{0};
    std::array<VkCommandBuffer, kSlotCount> command_buffers{};
    std::array<VkFence, kSlotCount> fences{};
    std::array<bool, kSlotCount> in_flight{};
    std::array<std::size_t, kSlotCount> submitted_pass_counts{};
    std::array<FrameDescriptorAllocator, kSlotCount>& descriptor_allocators;
    std::array<FrameDescriptorAllocator, kSlotCount>& text_tile_bin_allocators;
    std::array<FrameDescriptorAllocator, kSlotCount>& text_tile_raster_allocators;
    std::vector<VkDescriptorSet> descriptor_sets{};
    std::size_t pass_count{0};
    const runtime::CommandPlan* command_plan{nullptr};

    struct ReplaySlot {
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        bool in_flight{false};
        VulkanBufferAllocation params{};
    };
    static constexpr std::size_t kReplaySlotCount = 3;
    std::array<ReplaySlot, kReplaySlotCount> replay_slots{};
    bool replay_prepared{false};
    std::size_t replay_next_slot{0};

    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore timeline_semaphore{VK_NULL_HANDLE};
    std::uint64_t next_timeline_value{0};
    std::uint64_t pending_timeline_value{0};
    bool command_batch_active{false};
    bool command_batch_started{false};

    explicit VulkanSubmissionAuthority(VulkanDescriptorAuthority& descriptors)
        : descriptor_allocators(descriptors.pass),
          text_tile_bin_allocators(descriptors.text_tile_bin),
          text_tile_raster_allocators(descriptors.text_tile_raster) {}

    VulkanSubmissionAuthority(const VulkanSubmissionAuthority&) = delete;
    VulkanSubmissionAuthority& operator=(const VulkanSubmissionAuthority&) = delete;
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
