// vulkan_backend_cuda_surface_api_private.cpp — VulkanBackend CUDA/native-video
// surface adapters. This TU owns only the public adapter layer for CUDA
// external surfaces and video-encode aliases; the actual image/semaphore state
// remains owned by VulkanSurfaceAuthority and the Impl operations remain in
// vulkan_backend_cuda_export_private.cpp.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <cstring>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

graph::RenderOpResult VulkanBackend::create_cuda_external_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
    try {
        // Keep the existing cross-domain gate: CUDA surface creation mutates
        // SurfaceAuthority state and creates Vulkan-owned synchronization
        // objects, so it remains serialized with submission state.
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->create_cuda_external_surface(handle, desc);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
}

graph::RenderOpResult VulkanBackend::copy_surface_to_cuda_encoder(
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination,
    bool wait_for_completion) {
    try {
        // The copy/submit handoff uses the same Dual domain as before. This is
        // an adapter split only: no ownership or synchronization semantics are
        // moved out of VulkanBackend::Impl.
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->copy_surface_to_cuda_encoder(source, destination,
                                             wait_for_completion);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
}

graph::RenderOpResult VulkanBackend::prepare_cuda_surface_for_vulkan(
    runtime::RenderSurfaceHandle handle) {
    try {
        // The CUDA→Vulkan handoff remains a cross-domain operation; keep the
        // existing Dual lock rather than weakening the capability gate during
        // decomposition.
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->prepare_cuda_surface_for_vulkan(handle);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
}

CudaExternalMemoryInfo VulkanBackend::export_cuda_external_memory(
    runtime::RenderSurfaceHandle handle) const {
    std::lock_guard lock(m_impl->surfaces.mutex);
    return m_impl->export_cuda_external_memory(handle);
}

bool VulkanBackend::cuda_context_matches_device(CUcontext context) const noexcept {
    if (!context || !m_impl) return false;
    std::lock_guard lock(m_impl->surfaces.mutex);
    if (m_impl->physical_device == VK_NULL_HANDLE) return false;

    CUcontext previous = nullptr;
    CUdevice cuda_device{};
    CUuuid cuda_uuid{};
    if (cuCtxPushCurrent(context) != CUDA_SUCCESS) return false;
    const bool ok = cuCtxGetDevice(&cuda_device) == CUDA_SUCCESS &&
                    cuDeviceGetUuid(&cuda_uuid, cuda_device) == CUDA_SUCCESS;
    (void)cuCtxPopCurrent(&previous);
    if (!ok) return false;

    VkPhysicalDeviceIDProperties id_props{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &id_props;
    vkGetPhysicalDeviceProperties2(m_impl->physical_device, &properties);
    return std::memcmp(id_props.deviceUUID, cuda_uuid.bytes, VK_UUID_SIZE) == 0;
}

#endif // CHRONON3D_ENABLE_CUDA_INTEROP

// Video export remains a VulkanBackend API. These aliases deliberately route
// through the canonical CUDA adapter instead of creating a second video
// surface authority or a parallel encoder path.
graph::RenderOpResult VulkanBackend::create_video_encode_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    return create_cuda_external_surface(handle, desc);
#else
    (void)handle;
    (void)desc;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::create_video_encode_surface: CUDA interop is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::copy_surface_to_video_encode(
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    return copy_surface_to_cuda_encoder(source, destination);
#else
    (void)source;
    (void)destination;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::copy_surface_to_video_encode: CUDA interop is disabled"));
#endif
}

} // namespace chronon3d::backends::vulkan
