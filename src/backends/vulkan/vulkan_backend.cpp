#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/physical_framebuffer_allocation.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <array>

namespace chronon3d::backends::vulkan {

std::vector<VulkanDeviceInfo> VulkanBackend::enumerate_devices() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::vector<VulkanDeviceInfo> result;
    VkInstance instance = VK_NULL_HANDLE;
    const VkApplicationInfo app_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Chronon3D discovery",
        VK_MAKE_VERSION(0, 1, 0), "Chronon3D", VK_MAKE_VERSION(0, 1, 0),
        VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instance_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app_info,
        0, nullptr, 0, nullptr};
    if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
        return result;
    }
    std::uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS ||
        count == 0) {
        vkDestroyInstance(instance, nullptr);
        return result;
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return result;
    }
    for (std::uint32_t physical_index = 0; physical_index < devices.size(); ++physical_index) {
        const auto device = devices[physical_index];
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
        const bool graphics = std::any_of(
            families.begin(), families.end(), [](const auto& family) {
                return family.queueCount != 0 &&
                    (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            });
        if (!graphics) continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        std::array<std::uint8_t, 16> uuid{};
        bool has_uuid = false;
        std::uint32_t pci_domain = 0;
        std::uint32_t pci_bus = 0;
        std::uint32_t pci_device = 0;
        std::uint32_t pci_function = 0;
        bool has_pci_identity = false;
#if defined(VK_KHR_get_physical_device_properties2) || defined(VK_VERSION_1_1)
        VkPhysicalDeviceIDProperties id_properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
#ifdef VK_EXT_pci_bus_info
        VkPhysicalDevicePCIBusInfoPropertiesEXT pci_properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
        id_properties.pNext = &pci_properties;
#endif
        VkPhysicalDeviceProperties2 properties2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &id_properties;
        vkGetPhysicalDeviceProperties2(device, &properties2);
        std::copy(std::begin(id_properties.deviceUUID),
                  std::end(id_properties.deviceUUID), uuid.begin());
        has_uuid = std::any_of(uuid.begin(), uuid.end(),
                               [](std::uint8_t byte) { return byte != 0; });
#ifdef VK_EXT_pci_bus_info
        pci_domain = pci_properties.pciDomain;
        pci_bus = pci_properties.pciBus;
        pci_device = pci_properties.pciDevice;
        pci_function = pci_properties.pciFunction;
        has_pci_identity = pci_domain != 0 || pci_bus != 0 ||
                           pci_device != 0 || pci_function != 0;
#endif
#endif
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        std::uint64_t local_memory = 0;
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                local_memory += memory.memoryHeaps[i].size;
            }
        }
        VulkanDeviceInfo info;
        info.index = physical_index;
        info.name = properties.deviceName;
        info.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        info.device_memory_bytes = local_memory;
        info.device_uuid = uuid;
        info.has_device_uuid = has_uuid;
        info.pci_domain = pci_domain;
        info.pci_bus = pci_bus;
        info.pci_device = pci_device;
        info.pci_function = pci_function;
        info.has_pci_identity = has_pci_identity;
        result.push_back(std::move(info));
    }
    vkDestroyInstance(instance, nullptr);
    return result;
#else
    return {};
#endif
}

void VulkanBackend::begin_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    auto& batch = m_impl->frame_batch;
    if (batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_frame_batch: a frame batch is already active");
    }
    // Second and later frames of an active command batch keep recording into
    // the SAME command buffer (opened by the first frame).  Flush a
    // cross-overlay boundary barrier and reset only per-frame bookkeeping;
    // the descriptor allocator and command buffer stay intact so every
    // overlay's recorded descriptor sets remain valid until the single
    // submission at end_command_batch().
    if (m_impl->command_batch_active && m_impl->command_batch_started) {
        m_impl->emit_command_batch_boundary();
        batch.pass_count = 0;
        batch.sync_plan = nullptr;
        m_impl->clear_surface_access_state();
        batch.active = true;
        return;
    }
    const auto slot = batch.next_slot;
    // Wait ONLY on the fence of the slot being reused.  The other slots may
    // still be in flight; this is what bounds CPU-GPU overlap to the ring
    // size instead of stalling the whole device every frame.
    if (batch.in_flight[slot]) {
        const auto wait_start = profiling::now();
        // CPU-side fence wait — the honest fallback for GPU timing when
        // calibrated timestamps are unavailable (Fase 6): the wait shows on
        // the render thread track, no fake GPU bar is drawn.
        CHRONON_TRACE_SCOPE("chronon.gpu", "FenceWait");
        const VkResult wait_result = vkWaitForFences(
            m_impl->device, 1, &batch.fences[slot], VK_TRUE, UINT64_MAX);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            spdlog::error(
                "[vulkan] DEVICE LOST REPORT phase=begin_frame_batch slot={} next_slot={} "
                "in_flight_slots=[{},{},{}] pending_timeline={} command_batch_active={}",
                slot, batch.next_slot, batch.in_flight[0], batch.in_flight[1],
                batch.in_flight[2], m_impl->pending_timeline_value,
                m_impl->command_batch_active);
        }
        check(wait_result, "vkWaitForFences(frame batch slot)");
        ++m_impl->stats.frame_slot_wait_count;
        m_impl->stats.frame_slot_wait_us +=
            static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
        check(vkResetFences(m_impl->device, 1, &batch.fences[slot]),
              "vkResetFences(frame batch slot)");
        batch.in_flight[slot] = false;
        m_impl->read_gpu_timestamps(slot);
    }
    // Every recorded pass owns a descriptor set from this slot's allocator;
    // resetting it now is safe because the slot's previous submission (the
    // only one referencing those sets) has completed.
    m_impl->descriptor_arena.reset(slot);
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr};
    check(vkResetCommandBuffer(batch.command_buffers[slot], 0),
          "vkResetCommandBuffer(frame batch slot)");
    check(vkBeginCommandBuffer(batch.command_buffers[slot], &begin),
          "vkBeginCommandBuffer(frame batch slot)");
    if (m_impl->timestamp_pool != VK_NULL_HANDLE) {
        const auto query_base = static_cast<std::uint32_t>(2 * slot);
        vkCmdResetQueryPool(batch.command_buffers[slot], m_impl->timestamp_pool,
                            query_base, 2);
        vkCmdWriteTimestamp(batch.command_buffers[slot],
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_impl->timestamp_pool, query_base);
    }
    batch.active = true;
    batch.pass_count = 0;
    batch.descriptor_sets.clear();
    batch.sync_plan = nullptr;
    m_impl->clear_surface_access_state();
    // The first frame of a command batch opened the buffer above; mark the
    // batch as started so subsequent frames take the soft-reset path.
    if (m_impl->command_batch_active) {
        m_impl->command_batch_started = true;
    }
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::begin_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    if (m_impl->command_batch_active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a command batch is already active");
    }
    if (m_impl->frame_batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a frame batch is already active");
    }
    // The first overlay's begin_plan_batch → begin_frame_batch opens the
    // single command buffer for the whole batch.
    m_impl->command_batch_active = true;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::begin_plan_batch(const runtime::CommandPlan& plan) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    begin_frame_batch();
    // A previous frame may have kept its logical transient handles alive
    // until the encoder package released them.  begin_frame_batch() has just
    // waited for the ring slot that is about to be reused, so opportunistically
    // retire completed bindings now, before this plan adds the next frame's
    // aliases.  Without this boundary, plan-bound handles accumulated in the
    // surface store and every CPU fallback allocated another full-size image
    // until Vulkan ran out of device memory.
    m_impl->retire_completed_frame_transient_surfaces();
    m_impl->frame_batch.sync_plan = &plan.barriers;
    // Bind every planned allocation to its physical slot, backing each slot
    // with exactly one VkImage.  Lifetime-disjoint handles that share a
    // planned slot therefore alias the same device image (the registry-side
    // bind_plan_slots() propagates the same mapping for identity records).
    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle) continue;
        if (allocation.physical_slot == std::numeric_limits<std::size_t>::max()) continue;
        if (allocation.physical_slot >= plan.resources.slots.size()) continue;
        // Job-persistent surfaces (GPU asset/glyph atlases) are owned by the
        // asset cache and must never be rebound to a frame-transient planner
        // slot.  The old unconditional binding changed their lifetime to
        // FrameTransient, so end-of-job cleanup destroyed the Vulkan image
        // while the registry/cache still returned the logical handle.
        if (m_impl->surface_is_job_persistent(allocation.surface)) continue;
        const auto& planned = plan.resources.slots[allocation.physical_slot];
        // Physical slots are aliases, not descriptions of every logical
        // resource assigned to them.  The slot table can legitimately carry
        // the canvas-sized fallback dimensions, while a request is a tight
        // producer surface (text/overlay).  Using the slot dimensions here
        // promoted every aliased resource to a full 1920x1080 image and made
        // long Vulkan exports exhaust device memory.  Bind with the logical
        // request's real dimensions; the slot remains the alias identity.
        runtime::ResourceDesc request_desc{};
        if (allocation.request_index < plan.resources.requests.size()) {
            request_desc = plan.resources.requests[allocation.request_index].desc;
        }
        const auto width = request_desc.width != 0 ? request_desc.width : planned.desc.width;
        const auto height = request_desc.height != 0 ? request_desc.height : planned.desc.height;
        const auto format = request_desc.format.pixel != runtime::PixelFormat::Unknown
            ? request_desc.format : planned.desc.format;
        const auto usage = request_desc.usage != runtime::ResourceUsage::Generic
            ? request_desc.usage : planned.desc.usage;
        const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
            width, height, format, usage, runtime::LifetimeClass::FrameTransient);
        m_impl->bind_surface_to_slot(allocation.surface, allocation.physical_slot, desc);
    }
    // Rebinding a plan can leave the pre-plan pool slots orphaned. They are
    // not part of the compiled plan and must not inflate the physical-surface
    // pool or survive the frame as hidden compatibility allocations.
    m_impl->prune_surface_slots();
#else
    (void)plan;
    unsupported("begin_plan_batch");
#endif
}

void VulkanBackend::end_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    auto& batch = m_impl->frame_batch;
    if (!batch.active) return;
    if (m_impl->command_batch_active) {
        // Defer the submission: end_command_batch() performs exactly one
        // vkQueueSubmit for every overlay recorded into this command batch.
        batch.active = false;
        return;
    }
    m_impl->submit_batch();        m_impl->flush_deferred_surface_releases();
    batch.active = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::end_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    if (!m_impl->command_batch_active) return;
    if (m_impl->command_batch_started) {
        // The final frame's end_frame_batch() deferred its submission, so the
        // single command buffer is still open and holds all N overlays.  One
        // vkQueueSubmit flushes the whole batch.
        m_impl->submit_batch();
    }        m_impl->flush_deferred_surface_releases();
    m_impl->frame_batch.active = false;
    m_impl->command_batch_active = false;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

// ── Phase 8: command-replay public API ────────────────────────────────

std::size_t VulkanBackend::replay_slot_count() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return Impl::kReplaySlotCount;
#else
    return 0;
#endif
}

#ifdef CHRONON3D_ENABLE_VULKAN
VkCommandBuffer VulkanBackend::begin_replay_recording(std::size_t slot_index) {
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    return m_impl->begin_replay_recording(slot_index);
}
#endif

void VulkanBackend::end_replay_recording(std::size_t slot_index) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    m_impl->end_replay_recording(slot_index);
#else
    (void)slot_index;
#endif
}

void VulkanBackend::replay_submit(std::size_t slot_index,
                                   const void* params, std::size_t params_size) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    m_impl->replay_submit(slot_index, params,
                          static_cast<VkDeviceSize>(params_size));
#else
    (void)slot_index;
    (void)params;
    (void)params_size;
#endif
}

} // namespace chronon3d::backends::vulkan
