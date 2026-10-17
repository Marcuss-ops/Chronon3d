#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "src/backends/vulkan/memory/vulkan_memory_manager.hpp"

#include <array>
#include <vector>

TEST_CASE("Vulkan buffer allocation reports the logical VkBuffer capacity") {
    using namespace chronon3d::backends::vulkan;
    std::uint32_t layer_count = 0;
    REQUIRE(vkEnumerateInstanceLayerProperties(&layer_count, nullptr) == VK_SUCCESS);

    const VkApplicationInfo app_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Chronon3dBufferCapacityTest",
        VK_MAKE_VERSION(1, 0, 0), "Chronon3d", VK_MAKE_VERSION(1, 0, 0),
        VK_API_VERSION_1_1};
    const VkInstanceCreateInfo instance_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app_info,
        0, nullptr, 0, nullptr};
    VkInstance instance = VK_NULL_HANDLE;
    REQUIRE(vkCreateInstance(&instance_info, nullptr, &instance) == VK_SUCCESS);

    std::uint32_t physical_count = 0;
    REQUIRE(vkEnumeratePhysicalDevices(instance, &physical_count, nullptr) == VK_SUCCESS);
    if (physical_count == 0) {
        vkDestroyInstance(instance, nullptr);
        WARN("No Vulkan physical device available");
        return;
    }
    std::vector<VkPhysicalDevice> physical_devices(physical_count);
    REQUIRE(vkEnumeratePhysicalDevices(
        instance, &physical_count, physical_devices.data()) == VK_SUCCESS);

    VkPhysicalDevice selected = VK_NULL_HANDLE;
    std::uint32_t selected_family = 0;
    for (const auto physical : physical_devices) {
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count,
                                                  families.data());
        for (std::uint32_t i = 0; i < family_count; ++i) {
            if (families[i].queueCount &&
                (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                selected = physical;
                selected_family = i;
                break;
            }
        }
        if (selected != VK_NULL_HANDLE) break;
    }
    REQUIRE(selected != VK_NULL_HANDLE);

    constexpr float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
        selected_family, 1, &priority};
    const VkDeviceCreateInfo device_info{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0,
        1, &queue_info, 0, nullptr, 0, nullptr, nullptr};
    VkDevice device = VK_NULL_HANDLE;
    REQUIRE(vkCreateDevice(selected, &device_info, nullptr, &device) == VK_SUCCESS);

    VulkanMemoryManager manager;
    manager.initialize(instance, selected, device);
    constexpr VkDeviceSize logical_size = 152;
    const VkBufferCreateInfo buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, logical_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    auto buffer = manager.create_buffer(buffer_info, VulkanMemoryClass::DeviceLocal);
    CHECK(buffer.size == logical_size);
    manager.destroy_buffer(buffer);

    const VkBufferCreateInfo readback_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, logical_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    auto readback = manager.create_buffer(
        readback_info, VulkanMemoryClass::HostReadback);
    CHECK(readback.size == logical_size);
    CHECK(readback.mapped != nullptr);
    CHECK((readback.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
    CHECK((readback.memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(selected, &memory_properties);
    const bool has_cached_coherent_host_memory = [&] {
        for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            const auto flags = memory_properties.memoryTypes[i].propertyFlags;
            if ((flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                          VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                 VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                return true;
            }
        }
        return false;
    }();
    if (has_cached_coherent_host_memory) {
        CHECK((readback.memory_properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0);
    }
    manager.destroy_buffer(readback);
    manager.shutdown();
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
#endif
