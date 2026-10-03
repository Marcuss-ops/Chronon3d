// VulkanBackend upload surface adapters.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

graph::RenderOpResult VulkanBackend::upload_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::span<const float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        (void)m_impl->upload(handle, desc, rgba, true);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle; (void)desc; (void)rgba;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface_region(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height,
    std::span<const float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        (void)m_impl->upload_region(handle, desc, x, y, width, height, rgba, true);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle; (void)desc; (void)x; (void)y; (void)width; (void)height; (void)rgba;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface_region: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface_async(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::span<const float> rgba, runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        ticket.value = m_impl->upload(handle, desc, rgba, false);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        ticket = {};
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle; (void)desc; (void)rgba; (void)ticket;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface_async: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface_rows_async(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    const raster::BBox& clip, const UploadRows& rows,
    runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        ticket.value = m_impl->upload_rows(handle, desc, clip, rows, false);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        ticket = {};
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle; (void)desc; (void)clip; (void)rows; (void)ticket;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface_rows_async: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::wait_upload(const runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->submission_ring.mutex);
        if (!ticket.valid()) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::wait_upload: invalid ticket"));
        }
        if (ticket.value > m_impl->next_timeline_value) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::wait_upload: unknown ticket"));
        }
        m_impl->wait_upload_ticket(ticket.value);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)ticket;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::wait_upload: Vulkan support is disabled"));
#endif
}


} // namespace chronon3d::backends::vulkan
