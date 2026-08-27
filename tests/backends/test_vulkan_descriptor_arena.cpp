#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct VulkanContext {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    std::uint32_t queue_family{0};

    ~VulkanContext() {
        if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    }
};

VulkanContext make_context() {
    VulkanContext context;
    const VkApplicationInfo app_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Chronon3D descriptor tests",
        VK_MAKE_VERSION(1, 0, 0), "Chronon3D", VK_MAKE_VERSION(1, 0, 0),
        VK_API_VERSION_1_1};
    const VkInstanceCreateInfo instance_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app_info,
        0, nullptr, 0, nullptr};
    if (vkCreateInstance(&instance_info, nullptr, &context.instance) != VK_SUCCESS) {
        return context;
    }

    std::uint32_t device_count = 0;
    if (vkEnumeratePhysicalDevices(context.instance, &device_count, nullptr) != VK_SUCCESS ||
        device_count == 0) {
        return context;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    if (vkEnumeratePhysicalDevices(context.instance, &device_count, devices.data()) != VK_SUCCESS) {
        return context;
    }
    for (const auto device : devices) {
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
        for (std::uint32_t family = 0; family < family_count; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                families[family].queueCount != 0) {
                context.physical_device = device;
                context.queue_family = family;
                break;
            }
        }
        if (context.physical_device != VK_NULL_HANDLE) break;
    }
    if (context.physical_device == VK_NULL_HANDLE) return context;

    constexpr float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
        context.queue_family, 1, &priority};
    const VkDeviceCreateInfo device_info{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &queue_info,
        0, nullptr, 0, nullptr, nullptr};
    if (vkCreateDevice(context.physical_device, &device_info, nullptr,
                       &context.device) != VK_SUCCESS) {
        context.device = VK_NULL_HANDLE;
    }
    return context;
}

struct DescriptorPools {
    VkDevice device{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout{VK_NULL_HANDLE};
    std::vector<VkDescriptorPool> pools;
    std::size_t active_pool{0};

    static constexpr std::size_t initial_sets = 64;

    void grow() {
        const auto sets = initial_sets * (std::size_t{1} << pools.size());
        const std::array<VkDescriptorPoolSize, 2> sizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<std::uint32_t>(sets * 3)},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<std::uint32_t>(sets * 2)}}};
        const VkDescriptorPoolCreateInfo info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            static_cast<std::uint32_t>(sets), static_cast<std::uint32_t>(sizes.size()),
            sizes.data()};
        VkDescriptorPool pool = VK_NULL_HANDLE;
        REQUIRE(vkCreateDescriptorPool(device, &info, nullptr, &pool) == VK_SUCCESS);
        pools.push_back(pool);
    }

    VkDescriptorSet allocate() {
        if (active_pool >= pools.size()) grow();
        const VkDescriptorSetAllocateInfo info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
            pools[active_pool], 1, &layout};
        VkDescriptorSet set = VK_NULL_HANDLE;
        auto result = vkAllocateDescriptorSets(device, &info, &set);
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            ++active_pool;
            return allocate();
        }
        REQUIRE(result == VK_SUCCESS);
        return set;
    }

    void reset() {
        for (auto pool : pools) {
            REQUIRE(vkResetDescriptorPool(device, pool, 0) == VK_SUCCESS);
        }
        active_pool = 0;
    }

    ~DescriptorPools() {
        for (auto pool : pools) vkDestroyDescriptorPool(device, pool, nullptr);
        if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }
};

bool prepare(DescriptorPools& pools, VkDevice device) {
    pools.device = device;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    const VkDescriptorSetLayoutCreateInfo info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
        2, bindings};
    return vkCreateDescriptorSetLayout(device, &info, nullptr, &pools.layout) == VK_SUCCESS;
}

} // namespace

TEST_SUITE("VulkanDescriptorArena") {

TEST_CASE("allocates descriptor sets") {
    auto context = make_context();
    if (context.device == VK_NULL_HANDLE) { WARN("No Vulkan device available"); return; }
    DescriptorPools pools;
    REQUIRE(prepare(pools, context.device));
    CHECK(pools.allocate() != VK_NULL_HANDLE);
}

TEST_CASE("reset reuses existing descriptor pool") {
    auto context = make_context();
    if (context.device == VK_NULL_HANDLE) { WARN("No Vulkan device available"); return; }
    DescriptorPools pools;
    REQUIRE(prepare(pools, context.device));
    CHECK(pools.allocate() != VK_NULL_HANDLE);
    REQUIRE(pools.pools.size() == 1);
    const auto first_pool = pools.pools.front();
    pools.reset();
    CHECK(pools.pools.size() == 1);
    CHECK(pools.pools.front() == first_pool);
    CHECK(pools.allocate() != VK_NULL_HANDLE);
}

TEST_CASE("descriptor pools grow geometrically") {
    auto context = make_context();
    if (context.device == VK_NULL_HANDLE) { WARN("No Vulkan device available"); return; }
    DescriptorPools pools;
    REQUIRE(prepare(pools, context.device));
    for (std::size_t i = 0; i < 65; ++i) CHECK(pools.allocate() != VK_NULL_HANDLE);
    REQUIRE(pools.pools.size() >= 2);
    CHECK(pools.pools.size() == 2);
}

TEST_CASE("reset preserves all grown pools for subsequent reuse") {
    auto context = make_context();
    if (context.device == VK_NULL_HANDLE) { WARN("No Vulkan device available"); return; }
    DescriptorPools pools;
    REQUIRE(prepare(pools, context.device));
    for (std::size_t i = 0; i < 65; ++i) CHECK(pools.allocate() != VK_NULL_HANDLE);
    REQUIRE(pools.pools.size() == 2);
    const auto first = pools.pools[0];
    const auto second = pools.pools[1];
    pools.reset();
    CHECK(pools.pools.size() == 2);
    CHECK(pools.pools[0] == first);
    CHECK(pools.pools[1] == second);
    CHECK(pools.allocate() != VK_NULL_HANDLE);
    CHECK(pools.active_pool == 0);
}

} // TEST_SUITE
#endif
