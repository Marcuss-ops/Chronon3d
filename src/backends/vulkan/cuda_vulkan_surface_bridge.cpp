#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <stdexcept>
#include <string>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <chronon3d/core/profiling/profiling.hpp>

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

CUexternalSemaphore import_semaphore(int fd, const char* operation) {
    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
    desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
    desc.handle.fd = fd;
    CUexternalSemaphore semaphore = nullptr;
    const CUresult result = cuImportExternalSemaphore(&semaphore, &desc);
    // CUDA consumes the opaque FD on success. Close it only on failure; doing
    // so after a successful import can close an unrelated descriptor that was
    // allocated after the Vulkan export, corrupting filesystem/IPC state.
    if (result != CUDA_SUCCESS) close(fd);
    check_cuda(result, operation);
    return semaphore;
}

}  // namespace

CudaVulkanSurfaceBridge::CudaVulkanSurfaceBridge(
    const CudaExternalMemoryInfo& info, CUcontext context, CUstream stream)
    : m_context(context), m_stream(stream) {
    if (!m_context || info.fd < 0 || info.cuda_to_vulkan_semaphore_fd < 0 ||
        info.vulkan_to_cuda_semaphore_fd < 0 || info.width == 0 ||
        info.height == 0 || info.allocation_size == 0 ||
        (info.cuda_array_format != 1 && info.cuda_array_format != 2)) {
        throw std::invalid_argument("invalid CUDA/Vulkan external surface handles");
    }
    make_current();

    CUDA_EXTERNAL_MEMORY_HANDLE_DESC memory_desc{};
    memory_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    memory_desc.handle.fd = info.fd;
    memory_desc.size = info.allocation_size;
    CUresult result = cuImportExternalMemory(&m_memory, &memory_desc);
    // Ownership of the opaque FD transfers to CUDA on success.
    if (result != CUDA_SUCCESS) close(info.fd);
    check_cuda(result, "cuImportExternalMemory(surface)");

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC array_desc{};
    array_desc.offset = 0;
    array_desc.numLevels = 1;
    array_desc.arrayDesc.Width = info.width;
    array_desc.arrayDesc.Height = info.height;
    array_desc.arrayDesc.Depth = 0;
    array_desc.arrayDesc.Format = info.cuda_array_format == 2
            ? CU_AD_FORMAT_UNSIGNED_INT8 : CU_AD_FORMAT_FLOAT;
    array_desc.arrayDesc.NumChannels = 4;
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
            info.cuda_to_vulkan_semaphore_fd, "cuImportExternalSemaphore(cuda->vulkan)");
        m_vulkan_to_cuda = import_semaphore(
            info.vulkan_to_cuda_semaphore_fd, "cuImportExternalSemaphore(vulkan->cuda)");
    } catch (...) {
        if (m_vulkan_to_cuda) cuDestroyExternalSemaphore(m_vulkan_to_cuda);
        if (m_cuda_to_vulkan) cuDestroyExternalSemaphore(m_cuda_to_vulkan);
        if (m_mipmapped) cuMipmappedArrayDestroy(m_mipmapped);
        if (m_memory) cuDestroyExternalMemory(m_memory);
        throw;
    }
}

CudaVulkanSurfaceBridge::~CudaVulkanSurfaceBridge() {
    if (m_context) (void)cuCtxSetCurrent(m_context);
    // Destruction can happen after the owning compositor has stopped
    // submitting work but while the driver still has pending external
    // semaphore operations.  Retire the context here as the final lifetime
    // boundary before destroying imported CUDA objects.
    (void)cuCtxSynchronize();
    if (m_vulkan_to_cuda) cuDestroyExternalSemaphore(m_vulkan_to_cuda);
    if (m_cuda_to_vulkan) cuDestroyExternalSemaphore(m_cuda_to_vulkan);
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
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS params{};
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

}  // namespace chronon3d::backends::vulkan

#endif
