// End-to-end proof for the native Vulkan -> CUDA -> NVENC handoff.
//
// The image is allocated/exported by Vulkan, imported as a CUDA array, copied
// device-to-device into an FFmpeg CUDA frame, and submitted to h264_nvenc.
// No host pixel buffer is used. This is intentionally a probe first; Chronon
// can adopt the same ownership/synchronization contract after this gate passes.

#include <cuda.h>
#include <vulkan/vulkan.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace {
[[noreturn]] void fail(const char* what, int code = 1) {
    std::fprintf(stderr, "CUDA_VULKAN_NVENC_FAIL: %s (%d)\n", what, code);
    std::exit(code);
}
void vk_check(VkResult r, const char* what) { if (r != VK_SUCCESS) fail(what, r); }
void cu_check(CUresult r, const char* what) {
    if (r != CUDA_SUCCESS) {
        const char* n = nullptr; const char* s = nullptr;
        cuGetErrorName(r, &n); cuGetErrorString(r, &s);
        std::fprintf(stderr, "%s: %s (%s)\n", what, s ?: "unknown", n ?: "unknown");
        fail(what, static_cast<int>(r));
    }
}
void av_check(int r, const char* what) {
    if (r < 0) { char e[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(r, e, sizeof(e));
        std::fprintf(stderr, "%s: %s\n", what, e); fail(what, r); }
}
uint32_t memory_type(VkPhysicalDevice physical, uint32_t bits) {
    VkPhysicalDeviceMemoryProperties p{}; vkGetPhysicalDeviceMemoryProperties(physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
    fail("no device-local Vulkan memory type");
}
}

int main() {
    constexpr int W = 1920, H = 1080;
    cu_check(cuInit(0), "cuInit");
    CUdevice cuda_device{}; cu_check(cuDeviceGet(&cuda_device, 0), "cuDeviceGet");
    CUcontext cuda_context{};
#if defined(CUDA_VERSION) && CUDA_VERSION >= 13000
    cu_check(cuCtxCreate(&cuda_context, nullptr, 0, cuda_device), "cuCtxCreate");
#else
    cu_check(cuCtxCreate(&cuda_context, 0, cuda_device), "cuCtxCreate");
#endif

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "chronon-vulkan-nvenc-probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ii.pApplicationInfo = &app;
    VkInstance instance{}; vk_check(vkCreateInstance(&ii, nullptr, &instance), "vkCreateInstance");
    uint32_t count = 0; vk_check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(count); vk_check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = devices.front(); VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(physical, &props);
    std::fprintf(stderr, "device=%s\n", props.deviceName);
    uint32_t qcount = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(qcount); vkGetPhysicalDeviceQueueFamilyProperties(physical, &qcount, queues.data());
    uint32_t qfamily = 0; while (qfamily < qcount && !(queues[qfamily].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++qfamily;
    if (qfamily == qcount) fail("no Vulkan compute queue");
    const char* exts[] = {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME};
    float priority = 1.0f; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = qfamily; qi.queueCount = 1; qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 2; di.ppEnabledExtensionNames = exts;
    VkDevice vk_device{}; vk_check(vkCreateDevice(physical, &di, nullptr, &vk_device), "vkCreateDevice");

    VkExternalMemoryImageCreateInfo ei{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ei.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; image_info.pNext = &ei;
    image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent = {W, H, 1}; image_info.mipLevels = image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT; image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image{}; vk_check(vkCreateImage(vk_device, &image_info, nullptr, &image), "vkCreateImage");
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(vk_device, image, &req);
    VkExportMemoryAllocateInfo ex{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO}; ex.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.pNext = &ex; ai.allocationSize = req.size; ai.memoryTypeIndex = memory_type(physical, req.memoryTypeBits);
    VkDeviceMemory memory{}; vk_check(vkAllocateMemory(vk_device, &ai, nullptr, &memory), "vkAllocateMemory");
    vk_check(vkBindImageMemory(vk_device, image, memory, 0), "vkBindImageMemory");
    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(vk_device, "vkGetMemoryFdKHR")); if (!get_fd) fail("vkGetMemoryFdKHR");
    VkMemoryGetFdInfoKHR fi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR}; fi.memory = memory; fi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1; vk_check(get_fd(vk_device, &fi, &fd), "vkGetMemoryFdKHR");

    CUexternalMemory external{}; CUDA_EXTERNAL_MEMORY_HANDLE_DESC hd{}; hd.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD; hd.handle.fd = fd; hd.size = req.size;
    cu_check(cuImportExternalMemory(&external, &hd), "cuImportExternalMemory");
    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC md{}; md.arrayDesc.Width = W; md.arrayDesc.Height = H; md.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8; md.arrayDesc.NumChannels = 4; md.numLevels = 1;
    CUmipmappedArray mapped{}; cu_check(cuExternalMemoryGetMappedMipmappedArray(&mapped, external, &md), "cuExternalMemoryGetMappedMipmappedArray");
    CUarray source{}; cu_check(cuMipmappedArrayGetLevel(&source, mapped, 0), "cuMipmappedArrayGetLevel");

    AVBufferRef* cuda_device_ref = nullptr; av_check(av_hwdevice_ctx_create(&cuda_device_ref, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, 0), "av_hwdevice_ctx_create(cuda)");
    AVBufferRef* frames_ref = av_hwframe_ctx_alloc(cuda_device_ref); if (!frames_ref) fail("av_hwframe_ctx_alloc");
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frames_ref->data); frames->format = AV_PIX_FMT_CUDA; frames->sw_format = AV_PIX_FMT_BGR0; frames->width = W; frames->height = H; frames->initial_pool_size = 2;
    av_check(av_hwframe_ctx_init(frames_ref), "av_hwframe_ctx_init");
    AVFrame* frame = av_frame_alloc(); if (!frame) fail("av_frame_alloc"); frame->format = AV_PIX_FMT_CUDA; frame->width = W; frame->height = H; frame->hw_frames_ctx = av_buffer_ref(frames_ref);
    av_check(av_hwframe_get_buffer(frames_ref, frame, 0), "av_hwframe_get_buffer");
    CUDA_MEMCPY2D copy{}; copy.srcMemoryType = CU_MEMORYTYPE_ARRAY; copy.srcArray = source; copy.dstMemoryType = CU_MEMORYTYPE_DEVICE; copy.dstDevice = reinterpret_cast<CUdeviceptr>(frame->data[0]); copy.srcPitch = W * 4; copy.dstPitch = frame->linesize[0]; copy.WidthInBytes = W * 4; copy.Height = H;
    cu_check(cuMemcpy2D(&copy), "cuMemcpy2D Vulkan image -> AVHWFrames CUDA surface");

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc"); if (!codec) fail("h264_nvenc unavailable");
    AVCodecContext* cc = avcodec_alloc_context3(codec); if (!cc) fail("avcodec_alloc_context3"); cc->width = W; cc->height = H; cc->time_base = {1, 30}; cc->framerate = {30, 1}; cc->pix_fmt = AV_PIX_FMT_CUDA; cc->hw_frames_ctx = av_buffer_ref(frames_ref); av_check(avcodec_open2(cc, codec, nullptr), "avcodec_open2(h264_nvenc)");
    frame->pts = 0; av_check(avcodec_send_frame(cc, frame), "avcodec_send_frame");
    av_check(avcodec_send_frame(cc, nullptr), "avcodec_flush");
    AVPacket* packet = av_packet_alloc(); if (!packet) fail("av_packet_alloc");
    int packet_count = 0; int packet_bytes = 0;
    for (;;) {
        const int ret = avcodec_receive_packet(cc, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        av_check(ret, "avcodec_receive_packet");
        ++packet_count; packet_bytes += packet->size; av_packet_unref(packet);
    }
    if (packet_count == 0) fail("h264_nvenc produced no packet");
    std::printf("CUDA_VULKAN_NVENC_PASS: Vulkan image -> CUDA frame -> h264_nvenc, packets=%d packet_bytes=%d\n", packet_count, packet_bytes);
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&cc); av_buffer_unref(&frames_ref); av_buffer_unref(&cuda_device_ref);
    cuMipmappedArrayDestroy(mapped); cuDestroyExternalMemory(external); close(fd); vkDestroyImage(vk_device, image, nullptr); vkFreeMemory(vk_device, memory, nullptr); vkDestroyDevice(vk_device, nullptr); vkDestroyInstance(instance, nullptr); cuCtxDestroy(cuda_context); return 0;
}
