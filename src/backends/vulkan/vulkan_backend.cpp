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
    if (m_impl->command_batch_active && m_impl->command_batch_started) {
        m_impl->emit_command_batch_boundary();
        batch.pass_count = 0;
        batch.command_plan = nullptr;
        m_impl->clear_surface_access_state();
        batch.active = true;
        return;
    }
    const auto slot = batch.next_slot;
    if (batch.in_flight[slot]) {
        const auto wait_start = profiling::now();
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
    batch.command_plan = nullptr;
    m_impl->clear_surface_access_state();
    if (m_impl->command_batch_active) {
        m_impl->command_batch_started = true;
    }
#else
    (void)0;
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
    m_impl->command_batch_active = true;
    m_impl->command_batch_started = false;
#else
    (void)0;
#endif
}

void VulkanBackend::begin_plan_batch(const runtime::CommandPlan& plan) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    begin_frame_batch();
    m_impl->retire_completed_frame_transient_surfaces();

    // CommandPlan is the sole compiled synchronization authority. Vulkan
    // resolves transition.resource through plan.resources and translates the
    // canonical ResourceTransition stream directly to Synchronization2.
    m_impl->frame_batch.command_plan = &plan;

    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle) continue;
        if (allocation.physical_slot == std::numeric_limits<std::size_t>::max()) continue;
        if (allocation.physical_slot >= plan.resources.slots.size()) continue;
        if (m_impl->surface_is_job_persistent(allocation.surface)) continue;
        const auto& planned = plan.resources.slots[allocation.physical_slot];
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
        batch.active = false;
        return;
    }
    m_impl->submit_batch();
    m_impl->flush_deferred_surface_releases();
    batch.active = false;
#else
    (void)0;
#endif
}

void VulkanBackend::end_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->require_healthy();
    if (!m_impl->command_batch_active) return;
    if (m_impl->command_batch_started) {
        m_impl->submit_batch();
    }
    m_impl->flush_deferred_surface_releases();
    m_impl->frame_batch.active = false;
    m_impl->command_batch_active = false;
    m_impl->command_batch_started = false;
#else
    (void)0;
#endif
}

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
                                  const void* params,
                                  std::size_t params_size) {
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
