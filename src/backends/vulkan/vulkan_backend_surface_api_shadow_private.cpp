// vulkan_backend_surface_api_shadow_private.cpp — batched surface adapters for
// the production shadow-map passes.

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif
#include "vulkan_backend_surface_api_detail.hpp"

#include <array>
#include <span>

namespace chronon3d::backends::vulkan {

graph::RenderOpResult VulkanBackend::shadow_map_caster_surface(
    runtime::RenderSurfaceHandle depth_map,
    std::span<const float> light_space_vertices,
    std::span<const std::uint32_t> indices,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{depth_map};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->shadow_caster(depth_map, light_space_vertices, indices,
                              x0, y0, x1, y1);
    });
#else
    (void)depth_map;
    (void)light_space_vertices;
    (void)indices;
    (void)x0; (void)y0; (void)x1; (void)y1;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::shadow_map_caster_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::shadow_map_apply_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle depth_map,
    const rendering::ShadowApplyParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{
        destination, source, depth_map};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->shadow_apply(destination, source, depth_map, params);
    });
#else
    (void)destination; (void)source; (void)depth_map; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::shadow_map_apply_surface: Vulkan support is disabled"));
#endif
}

bool VulkanBackend::supports_shadow_map() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return m_impl &&
           m_impl->kernels.registry.contains(GpuKernelId::ShadowCaster) &&
           m_impl->kernels.registry.contains(GpuKernelId::ShadowApply);
#else
    return false;
#endif
}

} // namespace chronon3d::backends::vulkan
