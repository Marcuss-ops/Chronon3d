#include <chronon3d/runtime/gpu_runtime.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::runtime {

bool GpuRuntime::initialize(std::uint32_t device_id) {
    std::lock_guard lock(m_mutex);
    if (m_initialized) {
        return true;
    }

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (cuInit(0) == CUDA_SUCCESS) {
        CUdevice dev{};
        if (cuDeviceGet(&dev, static_cast<int>(device_id)) == CUDA_SUCCESS) {
            cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC);
            CUcontext ctx{};
            if (cuDevicePrimaryCtxRetain(&ctx, dev) == CUDA_SUCCESS) {
                cuCtxSetCurrent(ctx);
                CUstream stream{};
                if (cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING) == CUDA_SUCCESS) {
                    m_device_id = device_id;
                    m_native_context = reinterpret_cast<std::uintptr_t>(ctx);
                    m_default_stream = reinterpret_cast<std::uintptr_t>(stream);
                    m_api = GpuApiKind::Cuda;
                    m_initialized = true;
                    return true;
                }
                cuDevicePrimaryCtxRelease(dev);
            }
        }
    }
#endif

    return false;
}

void GpuRuntime::shutdown() noexcept {
    std::lock_guard lock(m_mutex);
    if (!m_initialized) return;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (m_api == GpuApiKind::Cuda) {
        if (m_default_stream) {
            cuStreamDestroy(reinterpret_cast<CUstream>(m_default_stream));
            m_default_stream = 0;
        }
        if (m_native_context) {
            CUdevice dev{};
            if (cuDeviceGet(&dev, static_cast<int>(m_device_id)) == CUDA_SUCCESS) {
                cuDevicePrimaryCtxRelease(dev);
            }
            m_native_context = 0;
        }
    }
#endif

    m_initialized = false;
    m_api = GpuApiKind::None;
}

} // namespace chronon3d::runtime
