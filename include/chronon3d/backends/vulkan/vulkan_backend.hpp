#pragma once

#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/vulkan/gpu_kernel_registry.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif

#include <memory>
#include <string>

namespace chronon3d::backends::vulkan {

struct VulkanBackendStats {
    std::string device_name{};
    bool discrete_gpu{false};
    std::uint64_t staging_allocations{0};
    std::uint64_t surface_creations{0};
    std::uint64_t surface_releases{0};
    std::uint64_t upload_calls{0};
    std::uint64_t upload_bytes{0};
    std::uint64_t readback_calls{0};
    std::uint64_t readback_bytes{0};
    std::uint64_t submissions{0};
};

/// Persistent headless Vulkan backend foundation. It owns the device and
/// queue for the runtime lifetime; graph surface execution is added on top of
/// this boundary and never exposes Vulkan handles to graph nodes.
class VulkanBackend final : public graph::RenderBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    VulkanBackend(VulkanBackend&&) noexcept;
    VulkanBackend& operator=(VulkanBackend&&) noexcept;

    [[nodiscard]] graph::RenderCapabilities capabilities() const noexcept override;
    [[nodiscard]] VulkanBackendStats stats() const noexcept;
    [[nodiscard]] const GpuKernelRegistry& kernel_registry() const noexcept;

    void apply_per_pixel_dof(
        Framebuffer&, std::span<const float>, const DepthOfFieldSettings&,
        const LensModel&, const std::optional<raster::BBox>&) override;
    void draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                   const Camera&, int, int) override;
    void apply_effect_stack(Framebuffer&, const EffectStack&,
                            const effects::EffectExecutionContext&) override;
    void composite_layer(Framebuffer&, const Framebuffer&, BlendMode,
                         const std::optional<raster::BBox>&,
                         CompositeOperator) override;
    void apply_blur(Framebuffer&, float,
                    const std::optional<raster::BBox>&) override;

    graph::RenderOpResult create_surface(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&) override;
    graph::RenderOpResult release_surface(
        runtime::RenderSurfaceHandle) override;
    graph::RenderOpResult upload_surface(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&,
        std::span<const float>) override;
    graph::RenderOpResult upload_surface_async(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&,
        std::span<const float>, runtime::UploadTicket&) override;
    graph::RenderOpResult wait_upload(const runtime::UploadTicket&) override;
    graph::RenderOpResult download_surface(
        runtime::RenderSurfaceHandle, std::span<float>) override;
    graph::RenderOpResult composite_surfaces(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        BlendMode, CompositeOperator) override;
    graph::RenderOpResult transform_surface(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        int, int, float) override;
    graph::RenderOpResult transform_surface_affine(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        const runtime::SurfaceAffineTransform&) override;
    graph::RenderOpResult blur_surface(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        float, bool) override;
    graph::RenderOpResult glow_surfaces(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        float, float, const Color&) override;
    graph::RenderOpResult color_adjust_surface(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        float, float, const Color&, float) override;
    graph::RenderOpResult matte_surface(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        runtime::RenderSurfaceHandle, bool, bool) override;

#ifdef CHRONON3D_ENABLE_VULKAN
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    [[nodiscard]] VkDevice device() const noexcept { return m_device; }
    [[nodiscard]] VkQueue graphics_queue() const noexcept { return m_queue; }
    [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept {
        return m_queue_family;
    }
#endif

private:
    [[noreturn]] static void unsupported(const char* operation);

#ifdef CHRONON3D_ENABLE_VULKAN
    VkInstance m_instance{VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    VkCommandPool m_command_pool{VK_NULL_HANDLE};
    std::uint32_t m_queue_family{0};
#endif
};

[[nodiscard]] std::unique_ptr<graph::RenderBackend> make_vulkan_backend();

} // namespace chronon3d::backends::vulkan
