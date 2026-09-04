// Runtime probe for the exact CUDA <-> Vulkan memory primitive required by
// the video bridge. It imports a Vulkan-exported opaque FD into CUDA; it does
// not claim NVDEC interop unless this probe succeeds.

#include <cuda.h>
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>

namespace chronon3d::profiling {
thread_local RenderCounters* g_current_counters = nullptr;
}

namespace {

[[noreturn]] void fail(const char* what, int code = 1) {
    std::fprintf(stderr, "CUDA_VULKAN_INTEROP_FAIL: %s (code=%d)\n", what, code);
    std::exit(code);
}

void vk_check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) fail(what, static_cast<int>(result));
}

void cu_check(CUresult result, const char* what) {
    if (result != CUDA_SUCCESS) {
        const char* name = nullptr;
        const char* text = nullptr;
        cuGetErrorName(result, &name);
        cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s: %s (%s)\n", what,
                     text ? text : "unknown", name ? name : "unknown");
        fail(what, static_cast<int>(result));
    }
}

} // namespace

int main() {
    cu_check(cuInit(0), "cuInit");

    uint32_t api_version = VK_API_VERSION_1_0;
    (void)vkEnumerateInstanceVersion(&api_version);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "chronon-cuda-vulkan-probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    vk_check(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance");

    uint32_t physical_count = 0;
    vk_check(vkEnumeratePhysicalDevices(instance, &physical_count, nullptr),
             "vkEnumeratePhysicalDevices(count)");
    if (physical_count == 0) fail("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicals(physical_count);
    vk_check(vkEnumeratePhysicalDevices(instance, &physical_count, physicals.data()),
             "vkEnumeratePhysicalDevices");

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    for (VkPhysicalDevice candidate : physicals) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (uint32_t i = 0; i < family_count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                physical = candidate;
                queue_family = i;
                std::fprintf(stderr, "device=%s\n", properties.deviceName);
                break;
            }
        }
        if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) fail("no compute-capable Vulkan device");

    const char* extensions[] = {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                                VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 4;
    device_info.ppEnabledExtensionNames = extensions;
    VkDevice device = VK_NULL_HANDLE;
    vk_check(vkCreateDevice(physical, &device_info, nullptr, &device), "vkCreateDevice");

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.flags = 0;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent = VkExtent3D{64, 64, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkExternalMemoryImageCreateInfo external_image{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external_image.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    image_info.pNext = &external_image;
    VkImage image = VK_NULL_HANDLE;
    vk_check(vkCreateImage(device, &image_info, nullptr, &image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type = i;
            break;
        }
    }
    if (memory_type == UINT32_MAX) fail("no device-local memory type");
    VkExportMemoryAllocateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    allocate_info.pNext = &export_info;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vk_check(vkAllocateMemory(device, &allocate_info, nullptr, &memory), "vkAllocateMemory");
    vk_check(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");

    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
    if (!get_fd) fail("vkGetMemoryFdKHR is unavailable");
    VkMemoryGetFdInfoKHR fd_info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    vk_check(get_fd(device, &fd_info, &fd), "vkGetMemoryFdKHR");

    CUdevice cuda_device = 0;
    cu_check(cuDeviceGet(&cuda_device, 0), "cuDeviceGet");
    CUcontext context = nullptr;
#if defined(CUDA_VERSION) && CUDA_VERSION >= 13000
    // CUDA 13 replaced the legacy flags/device overload with the v4
    // parameterized context creation entry point.
    cu_check(cuCtxCreate(&context, nullptr, 0, cuda_device), "cuCtxCreate");
#else
    cu_check(cuCtxCreate(&context, 0, cuda_device), "cuCtxCreate");
#endif
    VkExportSemaphoreCreateInfo semaphore_export{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    semaphore_export.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphore_info.pNext = &semaphore_export;
    VkSemaphore cuda_to_vulkan_semaphore = VK_NULL_HANDLE;
    vk_check(vkCreateSemaphore(device, &semaphore_info, nullptr,
                               &cuda_to_vulkan_semaphore),
             "vkCreateSemaphore(cuda to vulkan)");
    VkSemaphore vulkan_to_cuda_semaphore = VK_NULL_HANDLE;
    vk_check(vkCreateSemaphore(device, &semaphore_info, nullptr,
                               &vulkan_to_cuda_semaphore),
             "vkCreateSemaphore(vulkan to cuda)");
    auto get_semaphore_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
    if (!get_semaphore_fd) fail("vkGetSemaphoreFdKHR is unavailable");
    VkSemaphoreGetFdInfoKHR semaphore_fd_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    semaphore_fd_info.semaphore = cuda_to_vulkan_semaphore;
    semaphore_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    int cuda_to_vulkan_fd = -1;
    vk_check(get_semaphore_fd(device, &semaphore_fd_info, &cuda_to_vulkan_fd),
             "vkGetSemaphoreFdKHR(cuda to vulkan)");
    semaphore_fd_info.semaphore = vulkan_to_cuda_semaphore;
    int vulkan_to_cuda_fd = -1;
    vk_check(get_semaphore_fd(device, &semaphore_fd_info, &vulkan_to_cuda_fd),
             "vkGetSemaphoreFdKHR(vulkan to cuda)");

    {
        chronon3d::backends::vulkan::CudaExternalMemoryInfo handles{
            fd, cuda_to_vulkan_fd, vulkan_to_cuda_fd, requirements.size, 64, 64, 2};
        chronon3d::backends::vulkan::CudaVulkanSurfaceBridge bridge(
            handles, context);
        if (!bridge.array() || !bridge.surface_object()) {
            fail("CUDA bridge did not create a writable surface object");
        }
        // Exercise the actual CUDA→Vulkan release operation. The next Vulkan
        // submit would consume this binary semaphore.
        bridge.signal_for_vulkan();
        cu_check(cuStreamSynchronize(nullptr), "cuStreamSynchronize(signal)");
    }

    std::puts("CUDA_VULKAN_INTEROP_PASS: Chronon bridge mapped Vulkan image and signaled external semaphore");
    cuCtxDestroy(context);
    vkDestroySemaphore(device, cuda_to_vulkan_semaphore, nullptr);
    vkDestroySemaphore(device, vulkan_to_cuda_semaphore, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
