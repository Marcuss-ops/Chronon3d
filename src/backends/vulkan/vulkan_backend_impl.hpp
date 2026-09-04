#pragma once

// ============================================================================
// vulkan_backend_impl.hpp — PRIVATE header for VulkanBackend::Impl.
// ============================================================================

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/gpu_device_lost_error.hpp>
#include "memory/vulkan_memory_manager.hpp"
#include "debug/vulkan_debug_context.hpp"
#include "vulkan_descriptor_authority.hpp"
#include "vulkan_kernel_store.hpp"
#include "vulkan_submission_authority.hpp"
#include "vulkan_surface_authority.hpp"
#include "vulkan_upload_authority.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

inline bool surface_lifecycle_diag_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_SURFACE_LIFECYCLE_DIAG");
        return value && *value && std::string_view(value) != "0";
    }();
    return enabled;
}

inline void check(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return;
    if (result == VK_ERROR_DEVICE_LOST) {
        runtime::poison_gpu_worker();
        throw runtime::GpuDeviceLostError{
            std::string{"Vulkan device lost during "} + operation};
    }
    throw std::runtime_error(std::string{"Vulkan "} + operation +
                             " failed with VkResult=" +
                             std::to_string(static_cast<int>(result)));
}

inline constexpr float kIdentityTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};

struct VulkanBackend::Impl {
    mutable std::recursive_mutex api_mutex;

    void require_healthy() const {
        runtime::require_healthy_gpu_worker();
    }

    struct Image {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_R32G32B32A32_SFLOAT};
        std::uint32_t width{0};
        std::uint32_t height{0};
        runtime::TextAtlasEncoding text_atlas_encoding{
            runtime::TextAtlasEncoding::PremultipliedRGBA};
        bool initialized{false};
        bool exportable{false};
        VkSemaphore cuda_to_vulkan{VK_NULL_HANDLE};
        VkSemaphore vulkan_to_cuda{VK_NULL_HANDLE};
    };

    // Compatibility names remain inside Impl, but descriptor allocation and
    // descriptor handles now have exactly one owner: VulkanDescriptorAuthority.
    using FrameDescriptorAllocator = chronon3d::backends::vulkan::FrameDescriptorAllocator;
    using VulkanDescriptorArena = VulkanDescriptorAuthority;
    VulkanDescriptorArena descriptor_arena{};

    VkDescriptorSetLayout& descriptor_layout{descriptor_arena.descriptor_layout};
    VkDescriptorSetLayout& text_tile_bin_descriptor_layout{
        descriptor_arena.text_tile_bin_descriptor_layout};
    VkDescriptorSetLayout& text_tile_raster_descriptor_layout{
        descriptor_arena.text_tile_raster_descriptor_layout};
    VkDescriptorPool& descriptor_pool{descriptor_arena.descriptor_pool};
    VkDescriptorSet& descriptor_set{descriptor_arena.descriptor_set};
    std::array<VkDescriptorSet, 3>& glow_descriptor_sets{
        descriptor_arena.glow_descriptor_sets};

    // Submission ownership is likewise externalized. Existing call sites use
    // the historic member names only as references into this authority.
    using VulkanSubmissionRing = VulkanSubmissionAuthority;
    VulkanSubmissionRing submission_ring;
    using FrameBatchState = VulkanSubmissionRing;
    using ReplaySlot = VulkanSubmissionRing::ReplaySlot;
    static constexpr std::size_t kReplaySlotCount = VulkanSubmissionRing::kReplaySlotCount;
    std::array<ReplaySlot, kReplaySlotCount>& replay_slots{submission_ring.replay_slots};
    bool& replay_prepared{submission_ring.replay_prepared};
    std::size_t& replay_next_slot{submission_ring.replay_next_slot};

    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queue_family{0};
    VkCommandPool command_pool{VK_NULL_HANDLE};
    bool calibrated_ts_supported{false};
    PFN_vkGetCalibratedTimestampsEXT pfn_get_calibrated_timestamps{nullptr};
    bool gpu_timestamps_calibrated{false};
    std::uint64_t calibration_gpu_ts{0};
    std::int64_t calibration_cpu_trace_ns{0};
    std::uint64_t calibration_max_deviation{0};
    VkCommandBuffer& command_buffer{submission_ring.command_buffer};
    VkFence& fence{submission_ring.fence};
    VkSemaphore& timeline_semaphore{submission_ring.timeline_semaphore};
    VkQueryPool& timestamp_pool{submission_ring.timestamp_pool};
    std::uint64_t& next_timeline_value{submission_ring.next_timeline_value};
    std::uint64_t& pending_timeline_value{submission_ring.pending_timeline_value};

    VulkanKernelStore kernels{};

    using VulkanUploadRing = VulkanUploadAuthority;
    VulkanUploadRing uploads{};
    VulkanBufferAllocation& staging{uploads.staging};

    static constexpr std::size_t kGlyphInstancePassesPerSlot = 64;
    static constexpr std::size_t kGlyphInstanceRingSize =
        FrameBatchState::kSlotCount * kGlyphInstancePassesPerSlot;
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> glyph_instance_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> glyph_instance_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> glyph_instance_sizes{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> layer_instance_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> layer_instance_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> layer_instance_sizes{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_run_dynamic_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> text_run_dynamic_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> text_run_dynamic_sizes{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_tile_count_buffers{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_tile_index_buffers{};

    Image dst{};
    Image src{};

    // Source-compatibility façade only. All state and binding policy live in
    // VulkanSurfaceAuthority; this nested type keeps the existing out-of-line
    // destroy_all definition and therefore requires no duplicated store state.
    class VulkanSurfaceStore final
        : public VulkanSurfaceAuthority<Image, VulkanBackend::Impl> {
    public:
        void destroy_all(VulkanBackend::Impl& owner) noexcept;
    } surfaces;

    bool plan_preallocated{false};
    std::uint32_t plan_canvas_width{0};
    std::uint32_t plan_canvas_height{0};

    void destroy_surface_images() noexcept { surfaces.destroy_all(*this); }
    void defer_surface_release(runtime::RenderSurfaceHandle handle) {
        surfaces.deferred_surface_releases.push_back(handle);
    }
    [[nodiscard]] bool surface_valid(runtime::RenderSurfaceHandle handle) const noexcept {
        return surfaces.valid(handle);
    }
    [[nodiscard]] std::size_t surface_physical_count() const noexcept {
        return surfaces.physical_count();
    }
    void clear_surface_access_state() noexcept { surfaces.clear_access_state(); }
    [[nodiscard]] bool surface_is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
        return surfaces.is_job_persistent(handle);
    }
    Image& bind_surface_to_slot(runtime::RenderSurfaceHandle handle,
                                std::size_t slot,
                                const runtime::SurfaceDesc& desc) {
        return surfaces.bind(handle, slot, desc);
    }
    void prune_surface_slots() { surfaces.prune_unused_slots(); }

    VulkanSubmissionRing& frame_batch;
    bool& command_batch_active{submission_ring.command_batch_active};
    bool& command_batch_started{submission_ring.command_batch_started};
    float timestamp_period_ns{0.0f};
    std::uint32_t timestamp_valid_bits{0};
    VulkanMemoryManager memory_manager{};
    VulkanDebugContext* debug_context{nullptr};
    VulkanBackendStats stats{};

    Impl(VkInstance inst, VkPhysicalDevice physical, VkDevice logical,
         VkQueue graphics, std::uint32_t family, VkCommandPool pool,
         bool calibrated_timestamps_supported,
         VulkanDebugContext* dbg_ctx = nullptr);
    ~Impl();

    void destroy_image(Image& target);
    void destroy_upload_slot(VulkanUploadRing::UploadSlot& slot);
    void make_image(Image& target, std::uint32_t width, std::uint32_t height,
                    bool exportable = false,
                    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT);
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    VkSemaphore make_external_binary_semaphore();
    void create_cuda_external_surface(runtime::RenderSurfaceHandle handle,
                                      const runtime::SurfaceDesc& desc);
    [[nodiscard]] CudaExternalMemoryInfo export_cuda_external_memory(
        runtime::RenderSurfaceHandle handle) const;
    void prepare_cuda_surface_for_vulkan(runtime::RenderSurfaceHandle handle);
    void copy_surface_to_cuda_encoder(runtime::RenderSurfaceHandle source,
                                      runtime::RenderSurfaceHandle destination,
                                      bool wait_for_completion);
#endif

    void ensure_descriptor_set();
    void bind_descriptors(const Image& destination, const Image& source);
    void write_descriptors(VkDescriptorSet set,
                           const Image& destination, const Image& source);
    void write_fill_rect_descriptors(VkDescriptorSet set, const Image& destination);
    void bind_fill_rect_descriptors(const Image& destination);
    void write_matte_descriptors(VkDescriptorSet set, const Image& destination,
                                 const Image& target, const Image& matte);
    void write_text_run_descriptors(VkDescriptorSet set, const Image& destination,
                                    const Image& atlas, VkBuffer instance_buffer);
    void write_layer_batch_descriptors(VkDescriptorSet set, const Image& destination,
                                       const Image& source, VkBuffer instance_buffer);
    void write_text_batch_descriptors(VkDescriptorSet set, const Image& destination,
                                      const Image& atlas, VkBuffer glyph_buffer,
                                      VkBuffer run_buffer);
    VkDescriptorSet ensure_glow_descriptor_set(std::size_t index);
    [[nodiscard]] VkCommandBuffer active_command_buffer() const noexcept;
    VkDescriptorSet allocate_pass_descriptor_set();
    VkDescriptorSet allocate_text_tile_bin_descriptor_set();
    VkDescriptorSet allocate_text_tile_raster_descriptor_set();
    void write_text_tile_bin_descriptors(
        VkDescriptorSet set, VkBuffer glyph_buffer, VkBuffer run_buffer,
        VkBuffer tile_counts, VkBuffer tile_indices);
    void write_text_tile_raster_descriptors(
        VkDescriptorSet set, const Image& destination, const Image& atlas,
        VkBuffer glyph_buffer, VkBuffer run_buffer,
        VkBuffer tile_counts, VkBuffer tile_indices);

    // Single Vulkan synchronization boundary. Image transitions always arrive
    // as canonical ResourceTransition records; all native emission is Sync2.
    void emit_resource_transition(VkCommandBuffer command,
                                  VkImage image,
                                  const runtime::ResourceTransition& transition);
    void emit_plan_pass_transitions(VkCommandBuffer command,
                                    const runtime::CommandPlan& plan,
                                    std::size_t pass_index);
    void emit_unplanned_compute_sync(
        VkCommandBuffer command,
        std::initializer_list<const Image*> images);
    void emit_pass_sync(VkCommandBuffer command,
                        std::initializer_list<const Image*> images);
    void emit_buffer_barriers2(
        VkCommandBuffer command,
        std::span<const VkBufferMemoryBarrier2KHR> barriers);
    void emit_command_batch_boundary();
    void transition(VkCommandBuffer command, VkImage image,
                    VkImageLayout old_layout, VkImageLayout new_layout);

    static VkFormat to_vk_format(runtime::PixelFormat fmt) noexcept {
        switch (fmt) {
            case runtime::PixelFormat::Rgba32Float:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case runtime::PixelFormat::Rgba8Unorm:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case runtime::PixelFormat::R8Unorm:
                return VK_FORMAT_R8_UNORM;
            case runtime::PixelFormat::Nv12:
                return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
            case runtime::PixelFormat::P010:
                return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
            default:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    }

    static constexpr std::size_t pixel_format_bytes(runtime::PixelFormat fmt) noexcept {
        switch (fmt) {
            case runtime::PixelFormat::Rgba32Float: return 16;
            case runtime::PixelFormat::Rgba8Unorm: return 4;
            case runtime::PixelFormat::R8Unorm: return 1;
            case runtime::PixelFormat::Nv12: return 1;
            case runtime::PixelFormat::P010: return 2;
            default: return 16;
        }
    }

    static bool surface_compatible(const runtime::SurfaceDesc& a,
                                   const runtime::SurfaceDesc& b) {
        return a.width == b.width && a.height == b.height && a.format == b.format;
    }

    void preallocate_plan_surfaces(
        std::uint32_t canvas_width,
        std::uint32_t canvas_height,
        const graph::CompiledResourceTable& plan);
    bool slot_in_use(std::size_t slot) const;
    void prune_unused_transient_slots();
    [[nodiscard]] std::size_t bound_slot(runtime::RenderSurfaceHandle handle) const;
    Image& resolve_image(runtime::RenderSurfaceHandle handle);
    bool slot_has_initialized_occupant(std::size_t slot,
                                       runtime::RenderSurfaceHandle self) const;
    Image& bind_handle_to_slot(runtime::RenderSurfaceHandle handle,
                               std::size_t slot,
                               const runtime::SurfaceDesc& desc);
    Image& ensure_surface(runtime::RenderSurfaceHandle handle,
                          const runtime::SurfaceDesc& desc);
    void wait_upload_slot(VulkanUploadRing::UploadSlot& slot);
    VulkanUploadRing::UploadSlot& acquire_upload_slot(bool wait_for_completion);
    void ensure_upload_slot(VulkanUploadRing::UploadSlot& slot, VkDeviceSize bytes);
    void release_surface_now(runtime::RenderSurfaceHandle handle);
    void flush_deferred_surface_releases();
    void release_frame_transient_surfaces() noexcept;
    void retire_completed_frame_transient_surfaces() noexcept;
    std::uint64_t submit_upload(VulkanUploadRing::UploadSlot& slot,
                                bool wait_for_completion);
    std::uint64_t upload(runtime::RenderSurfaceHandle handle,
                         const runtime::SurfaceDesc& desc,
                         std::span<const float> rgba,
                         bool wait_for_completion);
    std::uint64_t upload_region(runtime::RenderSurfaceHandle handle,
                                const runtime::SurfaceDesc& desc,
                                std::int32_t x, std::int32_t y,
                                std::uint32_t width, std::uint32_t height,
                                std::span<const float> rgba,
                                bool wait_for_completion);
    void wait_upload_ticket(std::uint64_t ticket);
    void download(runtime::RenderSurfaceHandle handle, std::span<float> rgba);

    void record_composite(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          std::int32_t blend_mode, float source_scale,
                          const float tint[4],
                          const std::optional<raster::BBox>& clip = std::nullopt);
    void record_transform(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          int offset_x, int offset_y, float opacity);
    void record_transform_affine(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 const Image& destination, const Image& source,
                                 runtime::SurfaceAffineTransform transform);
    void record_blur(VkCommandBuffer command, VkDescriptorSet descriptors,
                     const Image& destination, const Image& source,
                     float radius, bool horizontal);
    void record_color_adjust(VkCommandBuffer command, VkDescriptorSet descriptors,
                             const Image& destination, const Image& source,
                             float brightness, float contrast,
                             const Color& tint, float tint_amount);
    void record_matte(VkCommandBuffer command, VkDescriptorSet descriptors,
                      const Image& destination, const Image& target,
                      const Image& matte, bool luma, bool inverted);
    void record_fill_rect(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination,
                          std::int32_t x0, std::int32_t y0,
                          std::int32_t x1, std::int32_t y1,
                          const Color& color,
                          const Vec4& shape = Vec4{0.0f},
                          const Vec4& line = Vec4{0.0f},
                          const std::array<Vec2, 8>& vertices = {});
    void record_text_run(VkCommandBuffer command, VkDescriptorSet descriptors,
                         const Image& destination, std::int32_t glyph_count,
                         VkBuffer instance_buffer, bool instance_updated,
                         float current_frame, const Color& highlight_color,
                         bool highlight_enabled,
                         std::int32_t dispatch_origin_x,
                         std::int32_t dispatch_origin_y,
                         std::int32_t dispatch_end_x,
                         std::int32_t dispatch_end_y);
    void record_layer_batch(VkCommandBuffer command, VkDescriptorSet descriptors,
                            const Image& destination, std::int32_t instance_count,
                            VkBuffer instance_buffer, bool instance_updated,
                            std::int32_t dispatch_origin_x,
                            std::int32_t dispatch_origin_y,
                            std::int32_t dispatch_end_x,
                            std::int32_t dispatch_end_y);
    void record_text_batch(VkCommandBuffer command, VkDescriptorSet descriptors,
                           const Image& destination, std::int32_t glyph_count,
                           std::int32_t run_count,
                           VkBuffer glyph_buffer, bool glyph_updated,
                           VkBuffer run_buffer, bool run_updated,
                           std::int32_t dispatch_origin_x,
                           std::int32_t dispatch_origin_y,
                           std::int32_t dispatch_end_x,
                           std::int32_t dispatch_end_y);
    void record_text_tile_bin(VkCommandBuffer command, VkDescriptorSet descriptors,
                              std::int32_t glyph_count, std::int32_t tiles_x,
                              std::int32_t tiles_y, std::int32_t max_glyphs_per_tile,
                              std::int32_t dst_width, std::int32_t dst_height);
    void record_text_tile_raster(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 std::int32_t tiles_x, std::int32_t tiles_y,
                                 std::int32_t max_glyphs_per_tile,
                                 const Image& destination, const Image& atlas);
    void read_gpu_timestamps(std::size_t slot);
    void submit_batch();
    void ensure_replay_params_capacity(ReplaySlot& slot, VkDeviceSize bytes);
    VkCommandBuffer begin_replay_recording(std::size_t slot_index);
    void end_replay_recording(std::size_t slot_index);
    void replay_submit(std::size_t slot_index,
                       const void* params, VkDeviceSize params_size);

    void composite(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source, BlendMode mode,
                   const std::optional<raster::BBox>& clip = std::nullopt,
                   bool replace = false);
    void fill_rect(runtime::RenderSurfaceHandle destination,
                   std::int32_t x0, std::int32_t y0,
                   std::int32_t x1, std::int32_t y1,
                   const Color& color);
    void initialize_transparent_surface(runtime::RenderSurfaceHandle destination);
    void fill_solid_shape(runtime::RenderSurfaceHandle destination,
                          std::int32_t x0, std::int32_t y0,
                          std::int32_t x1, std::int32_t y1,
                          const Vec4& shape, const Vec4& line,
                          const Color& color);
    void fill_path(runtime::RenderSurfaceHandle destination,
                   std::span<const Vec2> vertices, const Color& color);
    void transform(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source,
                   int offset_x, int offset_y, float opacity);
    void transform_affine(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle source,
                          const runtime::SurfaceAffineTransform& transform);
    void blur(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source, float radius, bool horizontal);
    void glow(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source,
              runtime::RenderSurfaceHandle scratch_horizontal,
              runtime::RenderSurfaceHandle scratch_vertical,
              float radius, float intensity, const Color& tint);
    void color_adjust(runtime::RenderSurfaceHandle destination,
                      runtime::RenderSurfaceHandle source,
                      float brightness, float contrast,
                      const Color& tint, float tint_amount);
    void matte(runtime::RenderSurfaceHandle destination,
               runtime::RenderSurfaceHandle target,
               runtime::RenderSurfaceHandle matte_surface,
               bool luma, bool inverted);
    void text_run_surface(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle atlas,
                          std::span<const runtime::GlyphInstance> glyphs,
                          float current_frame = 0.0f,
                          const Color& highlight_color = Color{},
                          bool highlight_enabled = false);
    void draw_text_batch(runtime::RenderSurfaceHandle destination,
                         std::span<const runtime::GlyphStatic> glyphs,
                         std::span<const runtime::TextRunDynamic> runs,
                         std::span<const runtime::RenderSurfaceHandle> atlas_pages);
    void execute_layer_batch(runtime::RenderSurfaceHandle destination,
                             std::span<const runtime::LayerInstance> instances,
                             std::span<const runtime::RenderSurfaceHandle> resources,
                             std::span<const float> transforms,
                             std::span<const float> paints);
    void ensure_images(std::uint32_t width, std::uint32_t height);
    void ensure_staging(VkDeviceSize bytes);
    void ensure_glyph_instance_buffer(VkDeviceSize bytes, std::size_t index);
    void ensure_layer_instance_buffer(VkDeviceSize bytes, std::size_t index);
    void ensure_text_run_dynamic_buffer(VkDeviceSize bytes, std::size_t index);
    void ensure_text_tile_buffer(VkDeviceSize bytes, std::size_t index,
                                 bool indices);
    void begin_command_buffer();
    void wait_for_pending();
    std::uint64_t submit(bool wait_for_completion = true);
    void composite(Framebuffer& destination, const Framebuffer& source);
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
