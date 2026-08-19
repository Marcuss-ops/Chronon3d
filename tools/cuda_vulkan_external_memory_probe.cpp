// Runtime probe for the exact CUDA <-> Vulkan memory primitive required by
// the video bridge. It imports a Vulkan-exported opaque FD into CUDA; it does
// not claim NVDEC interop unless this probe succeeds.

#include <cuda.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>

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
                                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 2;
    device_info.ppEnabledExtensionNames = extensions;
    VkDevice device = VK_NULL_HANDLE;
    vk_check(vkCreateDevice(physical, &device_info, nullptr, &device), "vkCreateDevice");

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.flags = 0;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
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
    cu_check(cuCtxCreate(&context, 0, cuda_device), "cuCtxCreate");
    CUexternalMemory external_memory = nullptr;
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC handle_desc{};
    handle_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    handle_desc.handle.fd = fd;
    handle_desc.size = requirements.size;
    cu_check(cuImportExternalMemory(&external_memory, &handle_desc),
             "cuImportExternalMemory");

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC array_desc{};
    array_desc.offset = 0;
    array_desc.arrayDesc.Width = 64;
    array_desc.arrayDesc.Height = 64;
    array_desc.arrayDesc.Depth = 0;
    array_desc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
    array_desc.arrayDesc.NumChannels = 4;
    array_desc.numLevels = 1;
    CUmipmappedArray mapped = nullptr;
    cu_check(cuExternalMemoryGetMappedMipmappedArray(&mapped, external_memory, &array_desc),
             "cuExternalMemoryGetMappedMipmappedArray");

    std::puts("CUDA_VULKAN_INTEROP_PASS: exported Vulkan image imported by CUDA");
    cuMipmappedArrayDestroy(mapped);
    cuDestroyExternalMemory(external_memory);
    cuCtxDestroy(context);
    close(fd);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
