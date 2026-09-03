// vulkan_backend_lifecycle.cpp — VulkanBackend public construction and move
// lifecycle (constructor, destructor, move).  Impl construction itself lives
// in vulkan_backend_lifecycle_private.cpp; this TU owns only the public
// wrapper so touching backend construction recompiles just this file.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

VulkanBackend::VulkanBackend(std::uint32_t requested_device_index) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const auto t_inst0 = std::chrono::steady_clock::now();
    m_debug_context = std::make_unique<VulkanDebugContext>();
    const auto debug_config = VulkanDebugConfig::from_environment();
    std::vector<const char*> enabled_layers;
    std::vector<const char*> enabled_instance_extensions;
    m_debug_context->configure_instance_requirements(
        enabled_layers, enabled_instance_extensions, debug_config);

    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Chronon3D",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Chronon3D",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_2};
    std::vector<VkValidationFeatureEnableEXT> validation_features;
    if (debug_config.enable_sync_validation) {
        validation_features.push_back(
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
    }
    if (debug_config.enable_gpu_assisted) {
        validation_features.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        validation_features.push_back(
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
    }
    const bool validation_features_extension_enabled =
        std::find(enabled_instance_extensions.begin(), enabled_instance_extensions.end(),
                  VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) !=
        enabled_instance_extensions.end();
    const VkValidationFeaturesEXT validation_features_info{
        VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        nullptr,
        static_cast<std::uint32_t>(validation_features.size()),
        validation_features.empty() ? nullptr : validation_features.data(),
        0,
        nullptr};
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = (validation_features.empty() || !validation_features_extension_enabled)
            ? nullptr : &validation_features_info,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<std::uint32_t>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(enabled_instance_extensions.size()),
        .ppEnabledExtensionNames = enabled_instance_extensions.empty() ? nullptr : enabled_instance_extensions.data()};
    check(vkCreateInstance(&instance_info, nullptr, &m_instance),
          "vkCreateInstance");

    m_debug_context->initialize(m_instance, debug_config);

    std::uint32_t device_count = 0;
    check(vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr),
          "vkEnumeratePhysicalDevices(count)");
    if (device_count == 0) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        throw std::runtime_error("Vulkan: no physical device available");
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    check(vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data()),
          "vkEnumeratePhysicalDevices");

    int best_score = -1;
    std::uint32_t graphics_device_index = 0;
    bool requested_device_selected = false;
    for (const auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        int device_score = 0;
        switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_score = 300; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_score = 200; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: device_score = 100; break;
            default: device_score = 0; break;
        }
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count,
                                                 families.data());
        for (std::uint32_t i = 0; i < family_count; ++i) {
            if (families[i].queueCount != 0 &&
                (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                if (requested_device_index != UINT32_MAX) {
                    if (graphics_device_index == requested_device_index) {
                        m_physical_device = device;
                        m_queue_family = i;
                        requested_device_selected = true;
                    }
                } else if (device_score > best_score) {
                    m_physical_device = device;
                    m_queue_family = i;
                    best_score = device_score;
                }
                ++graphics_device_index;
                break;
            }
        }
    }
    if (m_physical_device == VK_NULL_HANDLE ||
        (requested_device_index != UINT32_MAX && !requested_device_selected)) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        throw std::runtime_error(requested_device_index == UINT32_MAX
            ? "Vulkan: no graphics queue family available"
            : "Vulkan: requested graphics device index is unavailable");
    }

    m_init_instance_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t_inst0).count();
    const auto t_dev0 = std::chrono::steady_clock::now();

    constexpr float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = m_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority};
    const VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        nullptr, VK_TRUE};

    std::uint32_t device_extension_count = 0;
    vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr,
                                         &device_extension_count, nullptr);
    std::vector<VkExtensionProperties> device_extensions(device_extension_count);
    vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr,
                                         &device_extension_count,
                                         device_extensions.data());
    const auto has_device_extension = [&](const char* name) {
        return std::any_of(device_extensions.begin(), device_extensions.end(),
                           [name](const VkExtensionProperties& ext) {
                               return std::strcmp(ext.extensionName, name) == 0;
                           });
    };

    // Chronon targets Vulkan 1.2, therefore Synchronization2 is an explicit
    // backend requirement rather than a parallel optional barrier path.
    if (!has_device_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        throw std::runtime_error(
            "Vulkan: VK_KHR_synchronization2 is required by the compiled barrier plan");
    }
    VkPhysicalDeviceSynchronization2FeaturesKHR available_sync2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 available_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    available_features.pNext = &available_sync2;
    vkGetPhysicalDeviceFeatures2(m_physical_device, &available_features);
    if (available_sync2.synchronization2 != VK_TRUE) {
        throw std::runtime_error(
            "Vulkan: synchronization2 feature is unavailable on the selected device");
    }
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        &timeline_features,
        VK_TRUE};

    // VK_EXT_calibrated_timestamps — optional. When the device exposes it we
    // enable it so real GPU timestamps can be anchored on the Perfetto CPU
    // timeline. When absent, the backend reports only CPU-side timing.
    m_calibrated_timestamps_supported = false;
    for (const auto& ext : device_extensions) {
        if (std::strcmp(ext.extensionName,
                        VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0) {
            m_calibrated_timestamps_supported = true;
            break;
        }
    }
    std::vector<const char*> enabled_extensions{
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    const std::array<const char*, 4> cuda_extensions{
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
    const bool cuda_interop_extensions_available = std::all_of(
        cuda_extensions.begin(), cuda_extensions.end(), has_device_extension);
    if (cuda_interop_extensions_available) {
        enabled_extensions.insert(enabled_extensions.end(),
                                  cuda_extensions.begin(), cuda_extensions.end());
    } else {
        spdlog::warn("[Vulkan] CUDA interop extensions unavailable; native "
                     "video surface interop disabled");
    }
#endif
    if (m_calibrated_timestamps_supported) {
        enabled_extensions.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    }
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &synchronization2_features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount =
            static_cast<std::uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames =
            enabled_extensions.empty() ? nullptr : enabled_extensions.data(),
        .pEnabledFeatures = &features};
    check(vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device),
          "vkCreateDevice");
    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_queue_family};
    check(vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool),
          "vkCreateCommandPool");
    if (m_debug_context) {
        m_debug_context->set_device(m_device);
        m_debug_context->set_command_pool_name(m_command_pool, "Chronon3D.CommandPool.Main");
    }
    m_init_device_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t_dev0).count();
    const auto t_pipe0 = std::chrono::steady_clock::now();
    m_impl = std::make_unique<Impl>(m_instance, m_physical_device, m_device, m_queue,
                                    m_queue_family, m_command_pool,
                                    m_calibrated_timestamps_supported,
                                    m_debug_context.get());
    m_init_pipelines_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t_pipe0).count();
#else
    throw std::runtime_error("Vulkan backend was not compiled");
#endif
}

VulkanBackend::~VulkanBackend() {
#ifdef CHRONON3D_ENABLE_VULKAN
    m_impl.reset();
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        if (m_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);
        }
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_debug_context) m_debug_context->shutdown();
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
#endif
}

VulkanBackend::VulkanBackend(VulkanBackend&& other) noexcept {
    *this = std::move(other);
}

VulkanBackend& VulkanBackend::operator=(VulkanBackend&& other) noexcept {
    if (this == &other) return *this;
#ifdef CHRONON3D_ENABLE_VULKAN
    std::swap(m_impl, other.m_impl);
    std::swap(m_instance, other.m_instance);
    std::swap(m_physical_device, other.m_physical_device);
    std::swap(m_device, other.m_device);
    std::swap(m_queue, other.m_queue);
    std::swap(m_command_pool, other.m_command_pool);
    std::swap(m_queue_family, other.m_queue_family);
    std::swap(m_debug_context, other.m_debug_context);
#else
    (void)other;
#endif
    return *this;
}

} // namespace chronon3d::backends::vulkan
