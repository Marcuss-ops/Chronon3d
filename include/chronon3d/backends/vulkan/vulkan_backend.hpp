#pragma once

#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/vulkan/gpu_kernel_registry.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
struct CUctx_st;
typedef struct CUctx_st *CUcontext;
#endif

#include <memory>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

class VulkanDebugContext;

struct VulkanBackendStats {
    static constexpr std::size_t kUploadProducerCount =
        static_cast<std::size_t>(profiling::GpuUploadProducer::Count);
    std::string device_name{};
    bool discrete_gpu{false};
    std::uint64_t staging_allocations{0};
    std::uint64_t surface_creations{0};
    std::uint64_t surface_releases{0};
    std::uint64_t upload_calls{0};
    std::uint64_t upload_bytes{0};
    std::uint64_t upload_full_surface_bytes{0};
    std::uint64_t upload_region_bytes{0};
    std::array<std::uint64_t, kUploadProducerCount> upload_producer_bytes{};
    std::array<std::uint64_t, kUploadProducerCount> upload_producer_full_count{};
    std::array<std::uint64_t, kUploadProducerCount> upload_producer_region_count{};
    std::array<std::uint64_t, kUploadProducerCount> upload_producer_initial_count{};
    std::array<std::uint64_t, kUploadProducerCount> upload_producer_initial_bytes{};
    std::uint64_t readback_calls{0};
    std::uint64_t readback_bytes{0};
    std::uint64_t physical_surfaces_live{0};
    std::uint64_t surface_bindings_live{0};
    std::uint64_t deferred_surface_release_count{0};
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
    std::uint64_t standalone_wait_count{0};
    std::uint64_t standalone_wait_us{0};
    std::uint64_t frame_batch_drain_wait_count{0};
    std::uint64_t frame_batch_drain_wait_us{0};
    std::uint64_t frame_slot_wait_count{0};
    std::uint64_t frame_slot_wait_us{0};
    std::uint64_t readback_us{0};
    // GPU-elapsed duration measured with Vulkan timestamp queries (0 when
    // the device exposes no timestamp support). This is the GPU execution
    // time, distinct from the CPU-side submit/wait metrics above.
    std::uint64_t gpu_execute_us{0};
    // Detailed native Vulkan execution metrics
    std::uint64_t vk_cmd_dispatch_count{0};
    std::uint64_t vk_cmd_draw_count{0};
    std::uint64_t descriptor_allocations{0};
    std::uint64_t barriers_emitted{0};
    std::uint64_t layer_batch_calls{0};
    std::uint64_t layer_instances_processed{0};
    std::uint64_t text_batch_calls{0};
    std::uint64_t glyphs_processed{0};
    // VMA Memory Telemetry
    std::uint64_t vma_allocation_bytes{0};
    std::uint64_t vma_block_bytes{0};
    std::uint64_t vma_allocation_count{0};
    std::uint64_t vma_block_count{0};
    std::uint64_t vma_budget_bytes{0};
    std::uint64_t vma_usage_bytes{0};
    std::uint64_t gpu_asset_cache_hits{0};
    std::uint64_t gpu_asset_cache_misses{0};
    std::uint64_t gpu_asset_cache_initial_uploads{0};
    std::uint64_t gpu_asset_cache_initial_upload_bytes{0};
    std::uint64_t gpu_asset_cache_evictions{0};
    std::uint64_t gpu_asset_cache_evicted_bytes{0};
    std::uint64_t gpu_asset_cache_resident_bytes{0};
    bool command_batch_active{false};
    bool command_batch_started{false};
};

/// Hardware descriptors returned by the backend-owned physical-device
/// enumeration. Indices are stable for the lifetime of the Vulkan instance
/// and are the only IDs accepted by VulkanBackend's device selector.
struct VulkanDeviceInfo {
    std::uint32_t index{0};
    std::string name{};
    bool discrete{false};
    std::uint64_t device_memory_bytes{0};
    // Stable physical identity.  Vulkan enumeration indices are only local
    // ordering and must never be used to infer a CUDA ordinal.
    std::array<std::uint8_t, 16> device_uuid{};
    bool has_device_uuid{false};
    std::uint32_t pci_domain{0};
    std::uint32_t pci_bus{0};
    std::uint32_t pci_device{0};
    std::uint32_t pci_function{0};
    bool has_pci_identity{false};
};

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
struct CudaExternalMemoryInfo {
    int fd{-1};
    int cuda_to_vulkan_semaphore_fd{-1};
    int vulkan_to_cuda_semaphore_fd{-1};
    std::uint64_t allocation_size{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    // 1 = float4 (Chronon render surface), 2 = uchar4 (NVENC BGR0 surface).
    std::uint32_t cuda_array_format{1};
};
#endif

/// Persistent headless Vulkan backend foundation. It owns the device and
/// queue for the runtime lifetime; graph surface execution is added on top of
/// this boundary and never exposes Vulkan handles to graph nodes.
class VulkanBackend final : public graph::RenderBackend {
public:
    explicit VulkanBackend(std::uint32_t device_index = UINT32_MAX);
    ~VulkanBackend() override;

    [[nodiscard]] bool supports_native_video_surface() const noexcept override {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        return true;
#else
        return false;
#endif
    }
    [[nodiscard]] bool supports_native_surfaces() const noexcept override {
        return true;
    }
    graph::RenderOpResult create_video_encode_surface(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&) override;
    graph::RenderOpResult copy_surface_to_video_encode(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle) override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    VulkanBackend(VulkanBackend&&) noexcept;
    VulkanBackend& operator=(VulkanBackend&&) noexcept;

    [[nodiscard]] static std::vector<VulkanDeviceInfo> enumerate_devices();

    [[nodiscard]] graph::RenderCapabilities capabilities() const noexcept override;
    [[nodiscard]] std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    processor_snapshot() const noexcept override;
    [[nodiscard]] bool requires_processor_snapshot() const noexcept override;
    [[nodiscard]] VulkanBackendStats stats() const noexcept;
    [[nodiscard]] const GpuKernelRegistry& kernel_registry() const noexcept;

    /// Feeds the backend's GPU counters into the telemetry run record as
    /// name/value pairs (`gpu_submissions`, `passes_executed`,
    /// `gpu_submit_cpu_us`, `gpu_wait_cpu_us`, `frame_slot_wait_count`,
    /// `frame_slot_wait_us`, `readback_us`,
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
    /// Every planned pass consumes the canonical ResourceTransition stream
    /// already resolved by the runtime and translates it directly to Vulkan
    /// Synchronization2. Direct/unplanned surfaces use the explicit backend
    /// boundary rather than a second compiled barrier plan. The caller must
    /// invoke the surface operations in plan.passes order; pass_count doubles
    /// as the plan pass index. The backend also consumes plan.resources: each
    /// allocation's handle is bound to its planned physical slot and every
    /// slot is backed by exactly one VkImage, so lifetime-disjoint surfaces
    /// alias the same device memory.
    void begin_plan_batch(const runtime::CommandPlan& plan) override;

    /// Phase 5 — pre-allocate every physical surface from the compiler's
    /// interval-coloring plan.  Must be called once before the first frame
    /// (after prepare()).  After this call, every `create_surface()` in the
    /// frame loop is a direct handle→slot binding with zero vkCreateImage,
    /// zero vkCreateImageView, and zero vkAllocateMemory calls.
    ///
    /// The pool-based fallback path remains active for unplanned surfaces
    /// (job-persistent assets, scratch surfaces, text atlases).
    void preallocate_plan_surfaces(
        std::uint32_t canvas_width,
        std::uint32_t canvas_height,
        const graph::CompiledResourceTable& plan);

    // ── Phase 8: command-replay (record once, submit with param writes) ─
    /// Number of replay ring slots available.  Matches the frame-batch ring
    /// depth so the GPU overlap depth stays identical.
    [[nodiscard]] std::size_t replay_slot_count() const noexcept;
    /// Open a replay slot.  The returned command buffer is ready for
    /// recording (VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT).  The caller
    /// records every pass for one compiled frame into it, then calls
    /// end_replay_recording().  Must be called from prepare().
#ifdef CHRONON3D_ENABLE_VULKAN
    [[nodiscard]] VkCommandBuffer begin_replay_recording(std::size_t slot_index);
#endif
    /// Close the replay slot's command buffer so it can be submitted.
    void end_replay_recording(std::size_t slot_index);
    /// Submit a pre-recorded replay slot.  Writes `params` into the slot's
    /// persistently-mapped uniform buffer, then submits the pre-recorded
    /// command buffer with a single vkQueueSubmit.  Waits on the slot's
    /// fence if the previous frame is still in flight — same ring-depth
    /// bound as the live frame-batch path.
    void replay_submit(std::size_t slot_index,
                       const void* params, std::size_t params_size);

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
    graph::RenderOpResult draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                                    const Camera&, int, int) override;
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
    [[nodiscard]] bool is_native_surface_valid(
        runtime::RenderSurfaceHandle) const noexcept override;
    /// Waits for all Vulkan work that may reference native CUDA surfaces,
    /// without retiring or destroying those surfaces.
    [[nodiscard]] bool wait_for_pending_submissions() noexcept;
    /// Capacity of the native-import ring derived from the backend's actual
    /// frame-batch in-flight window.
    [[nodiscard]] std::size_t native_surface_ring_capacity() const noexcept;
    void release_frame_transient_surfaces() noexcept override;
    void retire_frame_transient_surfaces() noexcept override;
    graph::RenderOpResult upload_surface(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&,
        std::span<const float>) override;
    graph::RenderOpResult upload_surface_region(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&,
        std::int32_t, std::int32_t, std::uint32_t, std::uint32_t,
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
    graph::RenderOpResult copy_surface(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        const std::optional<raster::BBox>& = std::nullopt) override;
    graph::RenderOpResult fill_rect_surface(
        runtime::RenderSurfaceHandle, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t, const Color&) override;
    /// Native axis-aligned solid primitive fill. `primitive_kind` is 0 for
    /// rectangles, 1 for rounded rectangles and 2 for ellipses; radii are in
    /// destination pixels.
    graph::RenderOpResult fill_solid_shape_surface(
        runtime::RenderSurfaceHandle, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t, const Vec4& shape,
        const Vec4& line, const Color&);
    graph::RenderOpResult fill_path_surface(
        runtime::RenderSurfaceHandle, std::span<const Vec2>, const Color&);
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
    graph::RenderOpResult draw_text_run_surface_timed(
        runtime::RenderSurfaceHandle, runtime::RenderSurfaceHandle,
        std::span<const runtime::GlyphInstance>, float, const Color&, bool) override;

    /// Draw a text run directly into the final destination — zero
    /// intermediate surface, zero clear, zero composite after.
    graph::RenderOpResult draw_text_batch(
        runtime::RenderSurfaceHandle destination,
        std::span<const runtime::GlyphStatic> glyphs,
        std::span<const runtime::TextRunDynamic> runs,
        std::span<const runtime::RenderSurfaceHandle> atlas_pages) override;

    /// Execute a compiled GPU layer batch.  Every instance in `instances`
    /// is composited into `destination` in painter order.  `resources` maps
    /// resource_index→texture/atlas handle; `transforms` maps
    /// transform_index→4×4 matrix (flat float span, 16 floats per transform);
    /// `paints` maps paint_index→color uniform (flat float span, 8 floats
    /// per paint: r,g,b,a,brightness,contrast,tint_amount,flags).
    graph::RenderOpResult execute_layer_batch(
        runtime::RenderSurfaceHandle destination,
        std::span<const runtime::LayerInstance> instances,
        std::span<const runtime::RenderSurfaceHandle> resources,
        std::span<const float> transforms,
        std::span<const float> paints) override;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    graph::RenderOpResult create_cuda_external_surface(
        runtime::RenderSurfaceHandle, const runtime::SurfaceDesc&);
    graph::RenderOpResult copy_surface_to_cuda_encoder(
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination,
        bool wait_for_completion = true);
    graph::RenderOpResult prepare_cuda_surface_for_vulkan(
        runtime::RenderSurfaceHandle);
    [[nodiscard]] CudaExternalMemoryInfo export_cuda_external_memory(
        runtime::RenderSurfaceHandle) const;
    /// Returns true only when the CUDA context belongs to the same physical
    /// device selected by this Vulkan backend. External memory/semaphores are
    /// not portable across mismatched CUDA/Vulkan devices.
    [[nodiscard]] bool cuda_context_matches_device(CUcontext) const noexcept;
#endif

#ifdef CHRONON3D_ENABLE_VULKAN
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    [[nodiscard]] VkDevice device() const noexcept { return m_device; }
    [[nodiscard]] VkQueue graphics_queue() const noexcept { return m_queue; }
    [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept {
        return m_queue_family;
    }
    [[nodiscard]] double init_instance_ms() const noexcept { return m_init_instance_ms; }
    [[nodiscard]] double init_device_ms() const noexcept { return m_init_device_ms; }
    [[nodiscard]] double init_pipelines_ms() const noexcept { return m_init_pipelines_ms; }
#endif

private:
    template <typename Fn>
    graph::RenderOpResult run_batched_surface_op(Fn&& fn);
    void composite_legacy_surface(
        Framebuffer& destination, const Framebuffer& source, BlendMode mode,
        const std::optional<raster::BBox>& clip);
    [[noreturn]] static void unsupported(const char* operation);

#ifdef CHRONON3D_ENABLE_VULKAN
    VkInstance m_instance{VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    VkCommandPool m_command_pool{VK_NULL_HANDLE};
    std::uint32_t m_queue_family{0};
    // VK_EXT_calibrated_timestamps availability on the selected device,
    // detected at construction.  Enables real GPU→CPU timeline calibration
    // for the Perfetto "Chronon Vulkan Queue" track (Fase 6); when false the
    // backend reports only CPU-side submit/fence-wait events.
    bool m_calibrated_timestamps_supported{false};
    std::unique_ptr<VulkanDebugContext> m_debug_context;
    double m_init_instance_ms{0.0};
    double m_init_device_ms{0.0};
    double m_init_pipelines_ms{0.0};
#endif
};

[[nodiscard]] std::unique_ptr<graph::RenderBackend> make_vulkan_backend(
    std::uint32_t device_index = UINT32_MAX);

} // namespace chronon3d::backends::vulkan
