#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include "vulkan_descriptor_authority.hpp"
#include "memory/vulkan_memory_manager.hpp"

#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace chronon3d::backends::vulkan {

// Owns every Vulkan submission-lifetime primitive: rotating frame slots,
// replay slots and the immediate/timeline submission state. Descriptor
// allocators are borrowed from the descriptor authority rather than copied.
//
// Slot taxonomy:
//   frame slots  - command/fence lifetime for ordinary frame batches;
//   replay slots - pre-recorded command buffers + replay parameter buffers;
//   upload slots - NOT owned here; VulkanUploadAuthority owns transfer lifetime.
// Keeping upload slots separate prevents transfer staging lifetime from being
// coupled to graphics/compute submission rotation.
struct VulkanSubmissionAuthority {
    // One command-buffer/ring/queue state machine has one writer authority.
    // This lock serializes only that state machine; surface-only operations
    // do not need to wait on it.
    mutable std::mutex mutex;

    // B2 (TICKET-100X-PERF-WAVES): submission ring depth 6. The previous
    // depth of 3 forced the CPU to fence-wait on slot k-3 before recording
    // frame k, capping pipeline depth at ~2 frames and starving NVENC
    // (measured 73% avg utilization with frame_batch_drain_wait_us as the
    // top per-frame stall). Depth 6 matches the CUDA encode ring shape so
    // record frame k+2 while frame k executes becomes the steady state.
    // Backpressure is still bounded: when all slots are in flight the next
    // begin_frame_batch waits, exactly as before but later.
    static constexpr std::size_t kSlotCount = 6;
    static constexpr std::size_t kCompiledPassTimingCapacity = 256;

    bool active{false};
    std::size_t next_slot{0};
    std::array<VkCommandBuffer, kSlotCount> command_buffers{};
    std::array<VkFence, kSlotCount> fences{};
    std::array<bool, kSlotCount> in_flight{};
    std::array<std::size_t, kSlotCount> submitted_pass_counts{};
    std::array<std::uint64_t, kSlotCount> submitted_frame_indices{};
    std::array<bool, kSlotCount> submitted_frame_valid{};
    std::array<std::int64_t, kSlotCount> submitted_submit_end_trace_ns{};
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
    static constexpr std::size_t kReplaySlotCount = 6;
    std::array<ReplaySlot, kReplaySlotCount> replay_slots{};
    bool replay_prepared{false};
    std::size_t replay_next_slot{0};

    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore timeline_semaphore{VK_NULL_HANDLE};
    VkQueryPool timestamp_pool{VK_NULL_HANDLE};
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
