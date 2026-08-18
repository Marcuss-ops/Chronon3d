#pragma once

#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/vulkan/gpu_kernel_registry.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif

#include <memory>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
    std::uint64_t physical_surfaces_peak{0};
    std::uint64_t submissions{0};
    /// Cumulative number of GPU command-plan passes executed. Incremented
    /// per recorded pass (not per vkQueueSubmit), so it stays correct even
    /// when N overlays coalesce into a single command-batch submission.
    std::uint64_t passes_executed{0};
    // ── CPU-side GPU timing (microseconds) ────────────────────────────
    // `gpu_submit_cpu_us` is the CPU cost of vkQueueSubmit (recording is NOT
    // included); `gpu_wait_cpu_us` is CPU blocked on GPU fences — the
    // CPU→GPU synchronization point; `readback_us` is the map/memcpy/unmap
    // cost of a surface download (GPU→CPU transfer). These are CPU-wall
    // metrics and are deliberately distinct from GPU-elapsed timestamps.
    std::uint64_t gpu_submit_cpu_us{0};
    std::uint64_t gpu_wait_cpu_us{0};
    std::uint64_t readback_us{0};
    // GPU-elapsed duration measured with Vulkan timestamp queries (0 when
    // the device exposes no timestamp support). This is the GPU execution
    // time, distinct from the CPU-side submit/wait metrics above.
    std::uint64_t gpu_execute_us{0};
    // Compatibility bridge telemetry. These counters make the hybrid path
    // explicit instead of silently presenting CPU work as Vulkan work.
    std::uint64_t software_fallback_nodes{0};
    std::uint64_t software_fallback_us{0};
    std::uint64_t fallback_draw_node{0};
    std::uint64_t fallback_draw_image{0};
    std::uint64_t fallback_draw_other{0};
    std::uint64_t fallback_text_run{0};
    std::uint64_t fallback_composite{0};
    std::uint64_t fallback_composite_dimensions{0};
    std::uint64_t fallback_composite_mode{0};
    std::uint64_t fallback_effect{0};
    std::uint64_t fallback_blur{0};
    std::uint64_t fallback_dof{0};
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
    [[nodiscard]] std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    processor_snapshot() const noexcept override;
    [[nodiscard]] bool requires_processor_snapshot() const noexcept override;
    [[nodiscard]] VulkanBackendStats stats() const noexcept;
    [[nodiscard]] const GpuKernelRegistry& kernel_registry() const noexcept;

    /// Feeds the backend's GPU counters into the telemetry run record as
    /// name/value pairs (`gpu_submissions`, `passes_executed`,
    /// `gpu_submit_cpu_us`, `gpu_wait_cpu_us`, `readback_us`,
    /// `gpu_upload_bytes`, `gpu_readback_bytes`, `physical_surfaces_peak`,
    /// `cpu_gpu_sync_us`, `gpu_execute_us`).
    void export_gpu_telemetry_counters(
        std::vector<std::pair<std::string, std::uint64_t>>& out) const override;

    /// Number of distinct physical VkImages currently backing the surface
    /// bindings.  Plan-driven aliasing binds several logical handles to one
    /// physical slot, so this is the observable proof that lifetime-disjoint
    /// surfaces share device memory instead of allocating one image per
    /// handle.  Test-observable; not part of the base backend contract.
    [[nodiscard]] std::size_t physical_surface_count() const noexcept;

    /// Frame-batching lifecycle overrides.  While a frame batch is active
    /// every surface operation (composite, transform, blur, matte, glow,
    /// color adjust) records into a single command buffer and defers
    /// submission to end_frame_batch(), producing one vkQueueSubmit per
    /// frame.  Outside a batch the operations keep their immediate
    /// submit+wait semantics, preserving single-pass test compatibility.
    void begin_frame_batch() override;
    void end_frame_batch() override;

    /// Plan-driven frame-batch entry point for the command-plan executor.
    /// Like begin_frame_batch(), but while the batch is active every pass
    /// synchronizes through the BarrierPlan (precise compute-stage
    /// write→read / read→write / write→write barriers) instead of the
    /// conservative per-pass fallback used by direct op calls.  The caller
    /// must invoke the surface operations in plan.passes order; pass_count
    /// doubles as the plan pass index.  The backend also consumes
    /// plan.resources: each allocation's handle is bound to its planned
    /// physical slot and every slot is backed by exactly one VkImage, so
    /// lifetime-disjoint surfaces alias the same device memory.
    void begin_plan_batch(const runtime::CommandPlan& plan) override;

    /// Command-batch overrides.  While a command batch is active,
    /// end_frame_batch() no longer submits: it keeps recording into the
    /// single batch command buffer, so N overlays (each an execute_command_plan
    /// frame) accumulate.  end_command_batch() performs exactly one
    /// vkQueueSubmit for all of them.
    void begin_command_batch() override;
    void end_command_batch() override;

    void apply_per_pixel_dof(
        Framebuffer&, std::span<const float>, const DepthOfFieldSettings&,
        const LensModel&, const std::optional<raster::BBox>&) override;
    void draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                   const Camera&, int, int) override;
    /// Compatibility bridge for RenderNode shapes that have not yet migrated
    /// to the native RenderSurface API. The selected backend remains Vulkan;
    /// only the legacy node rasterisation is delegated to the canonical
    /// software backend.
    void set_draw_node_fallback(std::unique_ptr<graph::RenderBackend> fallback);
    void apply_effect_stack(Framebuffer&, const EffectStack&,
                            const effects::EffectExecutionContext&) override;
    void composite_layer(Framebuffer&, const Framebuffer&, BlendMode,
                         const std::optional<raster::BBox>&,
                         CompositeOperator) override;
    graph::RenderOpResult draw_text_run(
        Framebuffer&, const TextRunShape&, const glm::mat4&, float) override;
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
        BlendMode, CompositeOperator,
        const std::optional<raster::BBox>& = std::nullopt) override;
    graph::RenderOpResult fill_rect_surface(
        runtime::RenderSurfaceHandle, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t, const Color&) override;
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
    graph::RenderOpResult draw_text_run_surface(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        std::span<const runtime::GlyphInstance>) override;

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
    std::unique_ptr<graph::RenderBackend> m_draw_node_fallback;
    void record_software_fallback(
        const char* reason,
        std::chrono::steady_clock::time_point started) noexcept;
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
