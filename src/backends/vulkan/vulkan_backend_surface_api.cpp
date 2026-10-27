// vulkan_backend_surface_api.cpp — surface lifecycle, queries and download.
// Split from vulkan_backend_surface_api.cpp per issue #51; each adapter is a
// thin batched wrapper over the Impl ops.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif
#include "vulkan_backend_surface_api_detail.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

graph::RenderOpResult VulkanBackend::create_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->surfaces.mutex);
        (void)m_impl->ensure_surface(handle, desc);
        // Generic graph surfaces are transient even when created while a
        // batch is recording. Only CUDA/NVENC external surfaces are unplanned;
        // marking every in-batch surface unplanned prevents reclamation.
        ++m_impl->stats.surface_creations;
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::InvalidInput, error.what()));
    }
#else
    (void)handle; (void)desc;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::create_surface: Vulkan support is disabled"));
#endif
}


graph::RenderOpResult VulkanBackend::release_surface(
    runtime::RenderSurfaceHandle handle) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::release_surface: invalid handle"));
        }
        // A command buffer may still reference any surface created during the
        // active batch.  "Unplanned" only means that the barrier planner does
        // not know the handle; it must never shorten the Vulkan lifetime.
        if (surface_lifecycle_diag_enabled()) {
            spdlog::info("[surface-lifecycle] release handle={} defer={}", handle,
                         (m_impl->frame_batch.active || m_impl->command_batch_active));
        }
        if (m_impl->frame_batch.active || m_impl->command_batch_active) {
            m_impl->defer_surface_release(handle);
            return graph::RenderOpResult(graph::RenderOpOutcome{});
        }
        m_impl->release_surface_now(handle);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::release_surface: Vulkan support is disabled"));
#endif
}

bool VulkanBackend::is_native_surface_valid(
    runtime::RenderSurfaceHandle handle) const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl || handle == runtime::kInvalidRenderSurfaceHandle) return false;
    std::lock_guard lock(m_impl->surfaces.mutex);
    return m_impl->surface_valid(handle);
#else
    (void)handle;
    return false;
#endif
}

bool VulkanBackend::wait_for_pending_submissions() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return false;
    try {
        // Retiring completed frame transient surfaces requires the surface store lock.
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->wait_for_pending();
        return true;
    } catch (const std::exception& error) {
        spdlog::error("[vulkan] native submission drain failed: {}", error.what());
        return false;
    }
#else
    return false;
#endif
}

std::size_t VulkanBackend::native_surface_ring_capacity() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return Impl::VulkanSubmissionRing::kSlotCount + 1;
#else
    return 1;
#endif
}

void VulkanBackend::release_frame_transient_surfaces() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (m_impl) {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->release_frame_transient_surfaces();
    }
#endif
}

void VulkanBackend::retire_frame_transient_surfaces() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (m_impl) {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        // Keep deferred frame-batch releases alive through the terminal
        // native-output synchronization. The render pipeline calls this only
        // after its readback/encode consumer has finished with the output.
        m_impl->flush_deferred_surface_releases();
        m_impl->retire_completed_frame_transient_surfaces();
    }
#endif
}

graph::RenderOpResult VulkanBackend::download_surface(
    runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->download(handle, rgba);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle; (void)rgba;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::download_surface: Vulkan support is disabled"));
#endif
}
} // namespace chronon3d::backends::vulkan
