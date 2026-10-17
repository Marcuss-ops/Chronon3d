#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include "vulkan_external_fds.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <stdexcept>
#include <string>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <chronon3d/core/profiling/profiling.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d::backends::vulkan {
namespace {

[[nodiscard]] std::string cuda_error(CUresult result, const char* operation) {
    const char* name = nullptr;
    const char* text = nullptr;
    cuGetErrorName(result, &name);
    cuGetErrorString(result, &text);
    return std::string(operation) + ": " + (text ? text : "unknown") +
           " (" + (name ? name : "unknown") + ")";
}

void check_cuda(CUresult result, const char* operation) {
    if (result != CUDA_SUCCESS) {
        throw std::runtime_error(cuda_error(result, operation));
    }
}

CUexternalSemaphore import_semaphore(
    int fd, bool timeline, const char* operation) {
    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
    desc.type = timeline
        ? CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD
        : CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
    desc.handle.fd = fd;
    CUexternalSemaphore semaphore = nullptr;
    const CUresult result = cuImportExternalSemaphore(&semaphore, &desc);
    // The caller retains FD ownership on failure; CUDA consumes it on success.
    check_cuda(result, operation);
    return semaphore;
}

}  // namespace

CudaVulkanSurfaceBridge::CudaVulkanSurfaceBridge(
    const CudaExternalMemoryInfo& info, CUcontext context, CUstream stream)
    : m_context(context), m_stream(stream), m_width(info.width),
      m_height(info.height), m_cuda_array_format(info.cuda_array_format),
      m_timeline(info.timeline_semaphores),
      m_generation(info.generation) {
    VulkanExternalFds descriptors(info.fd, info.cuda_to_vulkan_semaphore_fd,
                                  info.vulkan_to_cuda_semaphore_fd);
    if (!m_context || info.fd < 0 || info.cuda_to_vulkan_semaphore_fd < 0 ||
        info.vulkan_to_cuda_semaphore_fd < 0 || info.width == 0 ||
        info.height == 0 || info.allocation_size == 0 ||
        (info.cuda_array_format != 1 && info.cuda_array_format != 2)) {
        throw std::invalid_argument("invalid CUDA/Vulkan external surface handles");
    }
    if (m_timeline && !m_generation) {
        throw std::invalid_argument(
            "timeline CUDA/Vulkan bridge requires the Vulkan interop generation authority");
    }
    m_wait_vulkan_fence = info.wait_vulkan_fence;
    make_current();

    CUDA_EXTERNAL_MEMORY_HANDLE_DESC memory_desc{};
    memory_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    memory_desc.handle.fd = info.fd;
    memory_desc.size = info.allocation_size;
    // VulkanMemoryManager allocates every exported image with dedicated
    // memory. CUDA must import that same allocation contract; omitting the
    // flag lets the driver interpret the tiled image as a suballocation.
    memory_desc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
    CUresult result = cuImportExternalMemory(&m_memory, &memory_desc);
    check_cuda(result, "cuImportExternalMemory(surface)");
    (void)descriptors.release(0);  // CUDA owns the imported memory FD.

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC array_desc{};
    array_desc.offset = 0;
    array_desc.numLevels = 1;
    array_desc.arrayDesc.Width = info.width;
    array_desc.arrayDesc.Height = info.height;
    array_desc.arrayDesc.Depth = 0;
    array_desc.arrayDesc.Format = info.cuda_array_format == 2
            ? CU_AD_FORMAT_UNSIGNED_INT8 : CU_AD_FORMAT_FLOAT;
    array_desc.arrayDesc.NumChannels = 4;
    array_desc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST;
    try {
        check_cuda(cuExternalMemoryGetMappedMipmappedArray(
                       &m_mipmapped, m_memory, &array_desc),
                   "cuExternalMemoryGetMappedMipmappedArray(surface)");
        check_cuda(cuMipmappedArrayGetLevel(&m_array, m_mipmapped, 0),
                   "cuMipmappedArrayGetLevel(surface)");
        CUDA_RESOURCE_DESC resource{};
        resource.resType = CU_RESOURCE_TYPE_ARRAY;
        resource.res.array.hArray = m_array;
        check_cuda(cuSurfObjectCreate(&m_surface, &resource),
                   "cuSurfObjectCreate(surface)");
        m_cuda_to_vulkan = import_semaphore(
            info.cuda_to_vulkan_semaphore_fd, m_timeline,
            "cuImportExternalSemaphore(cuda->vulkan)");
        (void)descriptors.release(1);
        m_vulkan_to_cuda = import_semaphore(
            info.vulkan_to_cuda_semaphore_fd, m_timeline,
            "cuImportExternalSemaphore(vulkan->cuda)");
        (void)descriptors.release(2);
    } catch (...) {
        if (m_vulkan_to_cuda) cuDestroyExternalSemaphore(m_vulkan_to_cuda);
        if (m_cuda_to_vulkan) cuDestroyExternalSemaphore(m_cuda_to_vulkan);
        if (m_surface) cuSurfObjectDestroy(m_surface);
        if (m_mipmapped) cuMipmappedArrayDestroy(m_mipmapped);
        if (m_memory) cuDestroyExternalMemory(m_memory);
        throw;
    }
}

CudaVulkanSurfaceBridge::~CudaVulkanSurfaceBridge() {
    if (m_context) (void)cuCtxSetCurrent(m_context);
    // The producer stream is synchronized by the owning compositor before
    // this bridge is destroyed. A global context synchronize here can wait on
    // the Vulkan side of an external semaphore and deadlock teardown.
    if (m_cuda_to_vulkan) cuDestroyExternalSemaphore(m_cuda_to_vulkan);
    if (m_vulkan_to_cuda) cuDestroyExternalSemaphore(m_vulkan_to_cuda);
    if (m_surface) cuSurfObjectDestroy(m_surface);
    if (m_mipmapped) cuMipmappedArrayDestroy(m_mipmapped);
    if (m_memory) cuDestroyExternalMemory(m_memory);
}

void CudaVulkanSurfaceBridge::make_current() {
    check_cuda(cuCtxSetCurrent(m_context), "cuCtxSetCurrent(surface bridge)");
}

void CudaVulkanSurfaceBridge::wait_for_vulkan(CUstream stream) {
    const auto started = std::chrono::steady_clock::now();
    make_current();
    const auto signal_value = m_wait_vulkan_fence
        ? m_wait_vulkan_fence() : (m_timeline ? m_generation->vulkan_to_cuda() : 0);
    if (std::getenv("CHRONON3D_CUDA_INTEROP_DIAG")) {
        spdlog::info("[cuda-interop-diag] cuda_wait sem={} value={} timeline={} stream={}",
            static_cast<void*>(m_vulkan_to_cuda), signal_value, m_timeline,
            static_cast<void*>(stream ? stream : m_stream));
    }
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS params{};
    if (m_timeline) {
        params.params.fence.value = signal_value;
    }
    check_cuda(cuWaitExternalSemaphoresAsync(
                   &m_vulkan_to_cuda, &params, 1, stream ? stream : m_stream),
               "cuWaitExternalSemaphoresAsync(vulkan->cuda)");
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_vulkan_wait_count.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_vulkan_wait_submit_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
}

void CudaVulkanSurfaceBridge::signal_for_vulkan(CUstream stream) {
    const auto started = std::chrono::steady_clock::now();
    make_current();
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS params{};
    if (m_timeline) {
        params.params.fence.value = m_generation->next_cuda_to_vulkan();
    }
    if (std::getenv("CHRONON3D_CUDA_INTEROP_DIAG")) {
        spdlog::info("[cuda-interop-diag] cuda_signal sem={} value={} timeline={} stream={}",
            static_cast<void*>(m_cuda_to_vulkan),
            m_timeline ? params.params.fence.value : 0, m_timeline,
            static_cast<void*>(stream ? stream : m_stream));
    }
    check_cuda(cuSignalExternalSemaphoresAsync(
                   &m_cuda_to_vulkan, &params, 1, stream ? stream : m_stream),
               "cuSignalExternalSemaphoresAsync(cuda->vulkan)");
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_vulkan_signal_count.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_vulkan_signal_submit_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
}

void CudaVulkanSurfaceBridge::copy_rgba8_from_cuda(
    CUdeviceptr source, std::size_t source_pitch, CUstream stream) {
    constexpr std::size_t kBytesPerPixel = 4;
    const auto row_bytes = static_cast<std::size_t>(m_width) * kBytesPerPixel;
    if (!source || m_cuda_array_format != 2 || m_width == 0 || m_height == 0 ||
        source_pitch < row_bytes) {
        throw std::invalid_argument(
            "RGBA8 CUDA handoff requires a valid device buffer, uchar4 Vulkan surface, "
            "and source pitch >= width * 4");
    }

    make_current();
    CUstream const ordered_stream = stream ? stream : m_stream;
    wait_for_vulkan(ordered_stream);

    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = source;
    copy.srcPitch = source_pitch;
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = m_array;
    copy.WidthInBytes = row_bytes;
    copy.Height = m_height;
    check_cuda(cuMemcpy2DAsync(&copy, ordered_stream),
               "cuMemcpy2DAsync(CUDA producer -> Vulkan surface)");
    signal_for_vulkan(ordered_stream);
}

}  // namespace chronon3d::backends::vulkan

#endif
