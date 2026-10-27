#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif
#include <chronon3d/core/profiling/profiling.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

void VulkanBackend::begin_frame_batch_unlocked() {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Caller holds the surface and submission domain locks. No re-entry.
    m_impl->require_healthy();
    // Diagnostic mode drains every prior Vulkan/CUDA submission before the
    // next frame. It distinguishes cross-frame ownership hazards from a bad
    // command recorded by an individual frame without changing the default
    // asynchronous ring behavior.
    if (std::getenv("CHRONON3D_VULKAN_SINGLE_FLIGHT") != nullptr) {
        m_impl->wait_for_pending();
    }
    auto& batch = m_impl->frame_batch;
    if (batch.active) {
        ++batch.nesting_depth;
        return;
    }
    if (m_impl->command_batch_active && m_impl->command_batch_started) {
        m_impl->emit_command_batch_boundary();
        batch.pass_count = 0;
        batch.command_plan = nullptr;
        batch.active = true;
        batch.nesting_depth = 1;
        return;
    }
    const auto slot = batch.next_slot;
    if (batch.in_flight[slot]) {
        const auto wait_start = profiling::now();
        CHRONON_TRACE_SCOPE("chronon.gpu", "FenceWait");
        const VkResult wait_result = vkWaitForFences(
            m_impl->device, 1, &batch.fences[slot], VK_TRUE, UINT64_MAX);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            std::string in_flight_states;
            for (std::size_t s = 0; s < Impl::FrameBatchState::kSlotCount; ++s) {
                in_flight_states += s == 0 ? "[" : ",";
                in_flight_states += batch.in_flight[s] ? "1" : "0";
            }
            in_flight_states += "]";
            spdlog::error(
                "[vulkan] DEVICE LOST REPORT phase=begin_frame_batch slot={} next_slot={} "
                "in_flight_slots={} pending_timeline={} command_batch_active={}",
                slot, batch.next_slot, in_flight_states,
                m_impl->pending_timeline_value,
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
        m_impl->retire_frame_slot_transient_surfaces(slot);
    }
    m_impl->descriptor_arena.reset(slot);
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr};
    check(vkResetCommandBuffer(batch.command_buffers[slot], 0),
          "vkResetCommandBuffer(frame batch slot)");
    check(vkBeginCommandBuffer(batch.command_buffers[slot], &begin),
          "vkBeginCommandBuffer(frame batch slot)");
    if (m_impl->timestamp_pool != VK_NULL_HANDLE) {
        const auto queries_per_slot = static_cast<std::uint32_t>(
            2 + 2 * Impl::FrameBatchState::kCompiledPassTimingCapacity);
        const auto query_base = static_cast<std::uint32_t>(slot) * queries_per_slot;
        vkCmdResetQueryPool(batch.command_buffers[slot], m_impl->timestamp_pool,
                            query_base, queries_per_slot);
        vkCmdWriteTimestamp(batch.command_buffers[slot],
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_impl->timestamp_pool, query_base);
    }
    batch.active = true;
    batch.nesting_depth = 1;
    batch.pass_count = 0;
    batch.descriptor_sets.clear();
    batch.command_plan = nullptr;
    if (m_impl->command_batch_active) {
        m_impl->command_batch_started = true;
    }
#else
    (void)0;
#endif
}

void VulkanBackend::begin_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->submission_ring.mutex);
    m_impl->require_healthy();
    if (m_impl->command_batch_active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a command batch is already active");
    }
    if (m_impl->frame_batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a frame batch is already active");
    }
    m_impl->command_batch_active = true;
    m_impl->command_batch_started = false;
#else
    (void)0;
#endif
}



void VulkanBackend::end_frame_batch_unlocked() {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Caller holds the surface and submission domain locks. No re-entry.
    m_impl->require_healthy();
    auto& batch = m_impl->frame_batch;
    if (!batch.active) return;
    if (batch.nesting_depth > 1) {
        --batch.nesting_depth;
        return;
    }
    // Audit N4: recording is over, so the plan-wide leases are released before
    // the submit/drain below (which need the store lock, not the leases).
    //
    // The release is the exact mirror of the pin in begin_plan_batch — same
    // list, same element count — because pin_recording_leases_locked() counts
    // per element while the map is keyed by handle: a duplicated surface in
    // resources.requests would otherwise keep its lease alive forever.
    if (!m_impl->plan_lease_scratch_.empty()) {
        m_impl->release_recording_leases_locked(m_impl->plan_lease_scratch_);
        m_impl->plan_lease_scratch_.clear();
    }
    // Safety net: at batch end no recording op can be in flight (both domains
    // are held), so any residual pin is a bug. Releasing it keeps a mistake
    // from wedging prune forever.
    m_impl->release_all_recording_leases_locked();
    if (m_impl->command_batch_active) {
        batch.active = false;
        batch.nesting_depth = 0;
        return;
    }
    m_impl->submit_batch();
    batch.active = false;
    batch.nesting_depth = 0;
#endif
}

void VulkanBackend::begin_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Public entry: synchronized ingress. The single-owner hot path calls
    // the *_unlocked core directly.
    // Audit N4 measurement: the two domains this site
    // still needs, timed (see Impl::DomainLock).
    Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
    begin_frame_batch_unlocked();
#else
    (void)0;
#endif
}

void VulkanBackend::end_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Public entry: synchronized ingress. The single-owner hot path calls
    // the *_unlocked core directly.
    // Audit N4 measurement: the two domains this site
    // still needs, timed (see Impl::DomainLock).
    Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
    end_frame_batch_unlocked();
#else
    (void)0;
#endif
}

void VulkanBackend::end_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Audit N4 measurement: the two domains this site
    // still needs, timed (see Impl::DomainLock).
    Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
    m_impl->require_healthy();
    if (!m_impl->command_batch_active) return;
    // Audit N4: end of the recording window — release the leases before the
    // submit/drain that follow.
    m_impl->release_all_recording_leases_locked();
    if (m_impl->command_batch_started) {
        m_impl->submit_batch();
    }
    m_impl->flush_deferred_surface_releases();
    m_impl->frame_batch.active = false;
    m_impl->frame_batch.nesting_depth = 0;
    m_impl->command_batch_active = false;
    m_impl->command_batch_started = false;
#else
    (void)0;
#endif
}


#ifdef CHRONON3D_ENABLE_VULKAN
VkCommandBuffer VulkanBackend::begin_replay_recording(std::size_t slot_index) {
    std::lock_guard lock(m_impl->submission_ring.mutex);
    m_impl->require_healthy();
    return m_impl->begin_replay_recording(slot_index);
}
#endif

void VulkanBackend::end_replay_recording(std::size_t slot_index) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->submission_ring.mutex);
    m_impl->require_healthy();
    m_impl->end_replay_recording(slot_index);
#else
    (void)slot_index;
#endif
}

void VulkanBackend::replay_submit(std::size_t slot_index,
                                  const void* params,
                                  std::size_t params_size) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->submission_ring.mutex);
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
