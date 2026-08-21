// Vulkan public backend construction and move lifecycle. Included by the backend TU to preserve the
// single public implementation while keeping each responsibility inspectable.
VulkanBackend::VulkanBackend() {
#ifdef CHRONON3D_ENABLE_VULKAN
    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Chronon3D",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Chronon3D",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr};
    check(vkCreateInstance(&instance_info, nullptr, &m_instance),
          "vkCreateInstance");

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
                if (device_score > best_score) {
                    m_physical_device = device;
                    m_queue_family = i;
                    best_score = device_score;
                }
                break;
            }
        }
    }
    if (m_physical_device == VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        throw std::runtime_error("Vulkan: no graphics queue family available");
    }

    constexpr float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = m_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority};
    const VkPhysicalDeviceFeatures features{};
    const VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        nullptr, VK_TRUE};
    // VK_EXT_calibrated_timestamps — optional. When the device exposes it we
    // enable it so real GPU timestamps can be anchored on the Perfetto CPU
    // timeline (Fase 6).  When absent, the backend keeps reporting only the
    // CPU-side submit/fence-wait events — never fake GPU bars.
    std::uint32_t device_extension_count = 0;
    vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr,
                                         &device_extension_count, nullptr);
    std::vector<VkExtensionProperties> device_extensions(device_extension_count);
    vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr,
                                         &device_extension_count,
                                         device_extensions.data());
    m_calibrated_timestamps_supported = false;
    for (const auto& ext : device_extensions) {
        if (std::strcmp(ext.extensionName,
                        VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0) {
            m_calibrated_timestamps_supported = true;
            break;
        }
    }
    std::vector<const char*> enabled_extensions;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    enabled_extensions.insert(enabled_extensions.end(), {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME});
#endif
    if (m_calibrated_timestamps_supported) {
        enabled_extensions.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    }
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &timeline_features,
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
    m_impl = std::make_unique<Impl>(m_physical_device, m_device, m_queue,
                                    m_queue_family, m_command_pool,
                                    m_calibrated_timestamps_supported);
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
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
#endif
}

VulkanBackend::VulkanBackend(VulkanBackend&& other) noexcept {
    *this = std::move(other);
}

VulkanBackend& VulkanBackend::operator=(VulkanBackend&& other) noexcept {
    if (this == &other) return *this;
    std::swap(m_draw_node_fallback, other.m_draw_node_fallback);
#ifdef CHRONON3D_ENABLE_VULKAN
    std::swap(m_impl, other.m_impl);
    std::swap(m_instance, other.m_instance);
    std::swap(m_physical_device, other.m_physical_device);
    std::swap(m_device, other.m_device);
    std::swap(m_queue, other.m_queue);
    std::swap(m_command_pool, other.m_command_pool);
    std::swap(m_queue_family, other.m_queue_family);
#else
    (void)other;
#endif
    return *this;
}

