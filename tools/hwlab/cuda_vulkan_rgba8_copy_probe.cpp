// GPU probe for CudaVulkanSurfaceBridge::copy_rgba8_from_cuda.
//
// Companion to cuda_vulkan_external_memory_probe.cpp: that one proved the
// bridge can map a Vulkan-exported image into CUDA; this one proves the RGBA8
// producer path end to end. Vulkan signals the vulkan-to-CUDA semaphore from
// a real queue submission, CUDA copies a device-resident pattern into the
// surface via copy_rgba8_from_cuda, and the pattern is read back from the
// mapped array and compared exactly. No host pixel buffer participates in the
// copy itself: the source lives in CUDA device memory and the destination is
// the Vulkan image, ordered on one stream. Rejection cases are asserted too:
// a too-small source pitch and a float surface must throw, not corrupt data.
//
// Exit code 0 = pass; prints CUDA_VULKAN_RGBA8_COPY_PASS on success.

#include <cuda.h>
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace chronon3d::profiling {
thread_local RenderCounters* g_current_counters = nullptr;
}

namespace {

constexpr uint32_t kW = 64;
constexpr uint32_t kH = 64;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kRowBytes = kW * kBytesPerPixel;
constexpr uint32_t kDevicePitch = 320;  // padded on purpose: pitch semantics

[[noreturn]] void fail(const char* what, int code = 1) {
    std::fprintf(stderr, "CUDA_VULKAN_RGBA8_COPY_FAIL: %s (code=%d)\n", what, code);
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

// Expected pattern: byte c of pixel (x, y) on row-major RGBA8 rows.
[[nodiscard]] inline unsigned char pattern(int layer, int x, int y) {
    return static_cast<unsigned char>((layer * 61 + y * 7 + x * 13) & 0xFF);
}

}  // namespace

int main() {
    cu_check(cuInit(0), "cuInit");

    uint32_t api_version = VK_API_VERSION_1_0;
    (void)vkEnumerateInstanceVersion(&api_version);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "chronon-cuda-vulkan-rgba8-copy-probe";
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

    auto allocate_exported_image = [&](VkFormat format, uint32_t array_format) {
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = format;
        image_info.extent = VkExtent3D{kW, kH, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkExternalMemoryImageCreateInfo external_image{
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
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
                (memory_properties.memoryTypes[i].propertyFlags &
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memory_type = i;
                break;
            }
        }
        if (memory_type == UINT32_MAX) fail("no device-local memory type");
        VkExportMemoryAllocateInfo export_info{
            VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate_info.allocationSize = requirements.size;
        allocate_info.memoryTypeIndex = memory_type;
        allocate_info.pNext = &export_info;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        vk_check(vkAllocateMemory(device, &allocate_info, nullptr, &memory),
                 "vkAllocateMemory");
        vk_check(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");

        auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
            vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
        if (!get_fd) fail("vkGetMemoryFdKHR is unavailable");
        VkMemoryGetFdInfoKHR fd_info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        fd_info.memory = memory;
        fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        int fd = -1;
        vk_check(get_fd(device, &fd_info, &fd), "vkGetMemoryFdKHR");
        return std::tuple(image, memory, fd, requirements.size, array_format);
    };

    auto [image, image_memory, image_fd, allocation_size, array_format] =
        allocate_exported_image(VK_FORMAT_B8G8R8A8_UNORM, 2);

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

    auto create_exported_semaphore = [&]() -> std::pair<VkSemaphore, int> {
        VkExportSemaphoreCreateInfo semaphore_export{
            VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
        semaphore_export.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        semaphore_info.pNext = &semaphore_export;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        vk_check(vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore),
                 "vkCreateSemaphore");
        auto get_semaphore_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
            vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
        if (!get_semaphore_fd) fail("vkGetSemaphoreFdKHR is unavailable");
        VkSemaphoreGetFdInfoKHR semaphore_fd_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
        semaphore_fd_info.semaphore = semaphore;
        semaphore_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        int fd = -1;
        vk_check(get_semaphore_fd(device, &semaphore_fd_info, &fd),
                 "vkGetSemaphoreFdKHR");
        return {semaphore, fd};
    };

    auto [cuda_to_vulkan_semaphore, cuda_to_vulkan_fd] = create_exported_semaphore();
    auto [vulkan_to_cuda_semaphore, vulkan_to_cuda_fd] = create_exported_semaphore();

    // Signal the vulkan_to_cuda semaphore from a real Vulkan queue submission
    // INSIDE the bridge scope: the CUDA import must observe an unsignaled
    // binary payload, and each CUDA wait consumes exactly one signal.

    {
        chronon3d::backends::vulkan::CudaExternalMemoryInfo handles{
            image_fd, cuda_to_vulkan_fd, vulkan_to_cuda_fd, allocation_size,
            kW, kH, array_format};
        chronon3d::backends::vulkan::CudaVulkanSurfaceBridge bridge(handles, context);
        if (!bridge.array() || !bridge.surface_object()) {
            fail("CUDA bridge did not create a writable surface object");
        }


        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex = queue_family;
        VkCommandPool pool = VK_NULL_HANDLE;
        vk_check(vkCreateCommandPool(device, &pool_info, nullptr, &pool),
                 "vkCreateCommandPool");
        VkCommandBufferAllocateInfo buffer_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        buffer_info.commandPool = pool;
        buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        buffer_info.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        vk_check(vkAllocateCommandBuffers(device, &buffer_info, &command),
                 "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vk_check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
        vk_check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &vulkan_to_cuda_semaphore;
        vk_check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        vk_check(vkQueueWaitIdle(queue), "vkQueueWaitIdle");

        // ── Device-resident known pattern, padded rows for pitch semantics ──
        // One extra source row: the second (shifted) copy reads rows 1..kH of
        // the source, where row kH is padding-only filler.
        CUdeviceptr device_source = 0;
        cu_check(cuMemAlloc(&device_source,
                            static_cast<std::size_t>(kH + 1) * kDevicePitch),
                 "cuMemAlloc(source)");
        std::vector<unsigned char> host_rows(
            static_cast<std::size_t>(kH + 1) * kDevicePitch, 0xEE);
        for (uint32_t y = 0; y < kH; ++y) {
            unsigned char* row =
                host_rows.data() + static_cast<std::size_t>(y) * kDevicePitch;
            for (uint32_t x = 0; x < kW; ++x) {
                for (int c = 0; c < static_cast<int>(kBytesPerPixel); ++c) {
                    row[static_cast<std::size_t>(x) * kBytesPerPixel + c]
                        = pattern(c, static_cast<int>(x), static_cast<int>(y));
                }
            }
            // Padding lanes between pixels and beyond the row stay 0xEE.
        }
        cu_check(cuMemcpyHtoD(device_source, host_rows.data(),
                              host_rows.size()),
                 "cuMemcpyHtoD(pattern)");

        // ── The path under test: device -> Vulkan surface, no host buffer ──
        try {
            bridge.copy_rgba8_from_cuda(device_source, kDevicePitch);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "copy_rgba8_from_cuda threw: %s\n", error.what());
            fail("copy_rgba8_from_cuda raised on valid input");
        }
        cu_check(cuStreamSynchronize(nullptr), "cuStreamSynchronize(copy)");

        // ── Read back the mapped array and compare exactly ────────────────
        std::vector<unsigned char> host_readback(
            static_cast<std::size_t>(kH) * kRowBytes, 0);
        CUDA_MEMCPY2D readback{};
        readback.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        readback.srcArray = bridge.array();
        readback.dstMemoryType = CU_MEMORYTYPE_HOST;
        readback.dstHost = host_readback.data();
        readback.dstPitch = kRowBytes;
        readback.WidthInBytes = kRowBytes;
        readback.Height = kH;
        cu_check(cuMemcpy2D(&readback), "cuMemcpy2D(array readback)");

        std::size_t mismatches = 0;
        std::size_t first_bad_row = 0, first_bad_col = 0, first_bad_byte = 0;
        for (uint32_t y = 0; y < kH; ++y) {
            for (uint32_t x = 0; x < kW; ++x) {
                for (int c = 0; c < static_cast<int>(kBytesPerPixel); ++c) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * kW + x) * kBytesPerPixel + c;
                    const unsigned char expected =
                        pattern(c, static_cast<int>(x), static_cast<int>(y));
                    if (host_readback[index] != expected) {
                        if (mismatches == 0) {
                            first_bad_row = y;
                            first_bad_col = x;
                            first_bad_byte = c;
                        }
                        ++mismatches;
                    }
                }
            }
        }
        if (mismatches != 0) {
            std::fprintf(stderr,
                         "first mismatch at row=%zu col=%zu byte-plane=%zu\n",
                         first_bad_row, first_bad_col, first_bad_byte);
            fail("pattern mismatch after copy_rgba8_from_cuda");
        }
        std::printf("pixel comparison: %zu/%zu bytes exact (pitched source, "
                    "array readback)\n", host_readback.size(), host_readback.size());

        // ── Rejection case 1: source pitch below row bytes must throw ─────
        bool pitch_rejected = false;
        try {
            bridge.copy_rgba8_from_cuda(device_source, kRowBytes - 1);
        } catch (const std::invalid_argument&) {
            pitch_rejected = true;
        }
        if (!pitch_rejected) fail("short pitch was accepted");

        // ── Rejection case 2: repeated valid copy stays deterministic ─────
        // A binary semaphore wait consumes one signal, so the Vulkan side
        // must re-signal vulkan_to_cuda before the second copy. This pins the
        // ownership protocol instead of racing the driver's payload state.
        vk_check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE),
                 "vkQueueSubmit(producer re-signal)");
        vk_check(vkQueueWaitIdle(queue), "vkQueueWaitIdle(producer re-signal)");
        memset(host_readback.data(), 0, host_readback.size());
        try {
            bridge.copy_rgba8_from_cuda(device_source + kDevicePitch, kDevicePitch);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "second copy threw: %s\n", error.what());
            fail("second copy_rgba8_from_cuda raised on valid input");
        }
        cu_check(cuStreamSynchronize(nullptr), "cuStreamSynchronize(second copy)");
        CUDA_MEMCPY2D readback2{};
        readback2.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        readback2.srcArray = bridge.array();
        readback2.dstMemoryType = CU_MEMORYTYPE_HOST;
        readback2.dstHost = host_readback.data();
        readback2.dstPitch = kRowBytes;
        readback2.WidthInBytes = kRowBytes;
        readback2.Height = kH;
        cu_check(cuMemcpy2D(&readback2), "cuMemcpy2D(second readback)");
        // After copying from source+pitch the surface holds the same bytes as
        // one row down, so surface row y must equal pattern row (y+1); the
        // last surface row is padding filler (0xEE) and is skipped here.
        std::size_t shift_mismatches = 0;
        for (uint32_t y = 0; y + 1 < kH; ++y) {
            for (uint32_t x = 0; x < kW; ++x) {
                for (int c = 0; c < static_cast<int>(kBytesPerPixel); ++c) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * kW + x) * kBytesPerPixel + c;
                    const unsigned char expected =
                        pattern(c, static_cast<int>(x), static_cast<int>(y) + 1);
                    if (host_readback[index] != expected) ++shift_mismatches;
                }
            }
        }
        if (shift_mismatches != 0) fail("second copy did not shift rows as expected");
        std::printf("second copy: row-shifted pattern reproduced exactly\n");

        // Device cleanup keeps the pool scoped with its queue/command buffers.
        vkDestroyCommandPool(device, pool, nullptr);

        cuMemFree(device_source);
    }

    // ── Rejection case 3: float surface must refuse the RGBA8 path ────────
    // Uses fresh semaphore FDs: the first bridge's copies leave its
    // cuda_to_vulkan payload signaled, and importing a signaled binary
    // external semaphore is invalid CUDA usage. The rejection being tested
    // fires on cuda_array_format, before any semaphore is touched.
    {
        auto [float_image, float_memory, float_fd, float_size, float_format] =
            allocate_exported_image(VK_FORMAT_R32G32B32A32_SFLOAT, 1);
        auto [float_cv_semaphore, float_cv_fd] = create_exported_semaphore();
        auto [float_vc_semaphore, float_vc_fd] = create_exported_semaphore();
        (void)float_image;
        (void)float_memory;
        chronon3d::backends::vulkan::CudaExternalMemoryInfo float_handles{
            float_fd, float_cv_fd, float_vc_fd, float_size,
            kW, kH, float_format};
        chronon3d::backends::vulkan::CudaVulkanSurfaceBridge float_bridge(
            float_handles, context);
        bool float_rejected = false;
        try {
            float_bridge.copy_rgba8_from_cuda(1, kRowBytes);
        } catch (const std::invalid_argument&) {
            float_rejected = true;
        }
        if (!float_rejected) fail("float surface accepted RGBA8 copy");
        std::printf("float surface: copy_rgba8_from_cuda rejected as required\n");
        vkDestroySemaphore(device, float_cv_semaphore, nullptr);
        vkDestroySemaphore(device, float_vc_semaphore, nullptr);
    }

    std::puts("CUDA_VULKAN_RGBA8_COPY_PASS: pattern copied device->Vulkan surface "
              "exactly and rejection cases held");
    cuCtxDestroy(context);
    vkDestroySemaphore(device, cuda_to_vulkan_semaphore, nullptr);
    vkDestroySemaphore(device, vulkan_to_cuda_semaphore, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, image_memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
