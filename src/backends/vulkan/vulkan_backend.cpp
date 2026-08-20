#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "composite_comp_spv.hpp"
#include "transform_comp_spv.hpp"
#include "affine_transform_comp_spv.hpp"
#include "blur_comp_spv.hpp"
#include "color_adjust_comp_spv.hpp"
#include "matte_comp_spv.hpp"
#include "text_run_comp_spv.hpp"
#include "fill_rect_comp_spv.hpp"
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

#ifdef CHRONON3D_ENABLE_VULKAN
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string{"Vulkan "} + operation +
                                 " failed with VkResult=" +
                                 std::to_string(static_cast<int>(result)));
    }
}

// Default tint for plain composite passes (no tint applied).
constexpr float kIdentityTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

struct VulkanBackend::Impl {
    // Graph execution is intentionally parallel, but command recording,
    // staging uploads and descriptor allocation are stateful per backend.
    // Serialize that narrow Vulkan boundary; CPU graph work remains parallel.
    mutable std::recursive_mutex api_mutex;
    struct Image {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_R32G32B32A32_SFLOAT};
        std::uint32_t width{0};
        std::uint32_t height{0};
        bool initialized{false};
        bool exportable{false};
        VkSemaphore cuda_to_vulkan{VK_NULL_HANDLE};
        VkSemaphore vulkan_to_cuda{VK_NULL_HANDLE};
    };

    /// Per-frame descriptor set allocator, one per frame-batch ring slot.
    /// Allocates one descriptor set per recorded pass from chunked pools
    /// that grow geometrically (64, 128, 256, ...) when a frame exceeds the
    /// current chunk.  reset() is called when the owning ring slot is
    /// reused, returning every chunk to its initial state so the pools are
    /// recycled across frames instead of reallocated.
    class FrameDescriptorAllocator {
    public:
        static constexpr std::size_t kInitialChunkSets = 64;
        // Worst case per set: the layout exposes 3 storage-image bindings
        // (matte uses all three; the other kernels use two) plus one
        // storage-buffer binding (the text-run kernel's glyph instances).
        // Pool sizing over-reserves so any pass can allocate safely.
        static constexpr std::size_t kStorageImagesPerSet = 3;
        static constexpr std::size_t kStorageBuffersPerSet = 1;

        void create(VkDevice device, VkDescriptorSetLayout layout) {
            device_ = device;
            layout_ = layout;
        }

        void destroy() {
            for (auto pool : pools_) {
                if (pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(device_, pool, nullptr);
                }
            }
            pools_.clear();
            active_pool_ = 0;
        }

        // Return every chunk to its initial state.  Only valid once the
        // owning ring slot's submission has completed (begin_frame_batch()
        // waits on that slot's fence first).
        void reset() {
            for (auto pool : pools_) {
                check(vkResetDescriptorPool(device_, pool, 0),
                      "vkResetDescriptorPool(frame descriptor chunk)");
            }
            active_pool_ = 0;
        }

        VkDescriptorSet allocate() {
            if (active_pool_ >= pools_.size()) grow();
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkDescriptorSetAllocateInfo allocation{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                pools_[active_pool_], 1, &layout_};
            const VkResult result =
                vkAllocateDescriptorSets(device_, &allocation, &set);
            if (result == VK_ERROR_OUT_OF_POOL_MEMORY ||
                result == VK_ERROR_FRAGMENTED_POOL) {
                // The current chunk is exhausted: move to the next chunk,
                // growing it geometrically if no chunk is left.
                ++active_pool_;
                return allocate();
            }
            check(result, "vkAllocateDescriptorSets(frame descriptor chunk)");
            return set;
        }

    private:
        void grow() {
            const std::size_t sets = kInitialChunkSets * (1u << pools_.size());
            const VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 static_cast<std::uint32_t>(sets * kStorageImagesPerSet)},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 static_cast<std::uint32_t>(sets * kStorageBuffersPerSet)}};
            const VkDescriptorPoolCreateInfo pool_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
                static_cast<std::uint32_t>(sets), 2, pool_sizes};
            VkDescriptorPool pool = VK_NULL_HANDLE;
            check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool),
                  "vkCreateDescriptorPool(frame descriptor chunk)");
            pools_.push_back(pool);
        }

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
        std::vector<VkDescriptorPool> pools_{};
        std::size_t active_pool_{0};
    };

    /// State of the frame-batch ring.  While active, surface operations
    /// only record commands into the current slot's command buffer (never
    /// submit); the single vkQueueSubmit for the frame happens in
    /// submit_batch() called from end_frame_batch().  Each slot owns its own
    /// command buffer, fence and descriptor allocator so begin_frame_batch()
    /// waits ONLY on the fence of the slot it is about to reuse (never
    /// vkDeviceWaitIdle per frame) and resets only that slot's allocator —
    /// batches still in flight on the other slots keep their descriptor
    /// sets valid.  The ring bounds CPU-GPU overlap: slot N is reused after
    /// kSlotCount batches, so recording can run up to kSlotCount - 1 frames
    /// ahead of GPU execution.
    struct FrameBatchState {
        static constexpr std::size_t kSlotCount = 3;
        bool active{false};
        std::size_t next_slot{0};
        std::array<VkCommandBuffer, kSlotCount> command_buffers{};
        std::array<VkFence, kSlotCount> fences{};
        std::array<bool, kSlotCount> in_flight{};
        std::array<FrameDescriptorAllocator, kSlotCount> descriptor_allocators{};
        std::vector<VkDescriptorSet> descriptor_sets;
        std::size_t pass_count{0};
        // When set, the batch is plan-driven: ops synchronize through this
        // BarrierPlan (via begin_plan_batch) instead of the conservative
        // fallback.  pass_count doubles as the plan pass index: ops are
        // called in plan order and each op advances it by one.
        const runtime::BarrierPlan* sync_plan{nullptr};
    };

    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    std::uint32_t queue_family;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore timeline_semaphore{VK_NULL_HANDLE};
    std::uint64_t next_timeline_value{0};
    std::uint64_t pending_timeline_value{0};
    VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, 3> glow_descriptor_sets{};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    GpuKernelRegistry kernel_registry{};
    VkBuffer staging{VK_NULL_HANDLE};
    VkDeviceMemory staging_memory{VK_NULL_HANDLE};
    VkDeviceSize staging_capacity{0};
    // Device-local storage buffers for glyph instances. A frame can contain
    // multiple TextRun passes (watermark + subtitles); each pass gets its own
    // buffer so one in-command-buffer update can never overwrite data read by
    // an earlier dispatch. The pass ring is deliberately generous and is
    // recycled only with the owning frame-batch slot.
    static constexpr std::size_t kGlyphInstancePassesPerSlot = 64;
    static constexpr std::size_t kGlyphInstanceRingSize =
        FrameBatchState::kSlotCount * kGlyphInstancePassesPerSlot;
    std::array<VkBuffer, kGlyphInstanceRingSize> glyph_instance_buffers{};
    std::array<VkDeviceMemory, kGlyphInstanceRingSize> glyph_instance_memories{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> glyph_instance_capacities{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> glyph_instance_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> glyph_instance_sizes{};
    struct UploadSlot {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkDeviceSize capacity{0};
        std::uint64_t ticket{0};
        bool in_flight{false};
    };
    static constexpr std::size_t kUploadSlotCount = 3;
    std::array<UploadSlot, kUploadSlotCount> upload_slots{};
    std::size_t next_upload_slot{0};
    Image dst{};
    Image src{};

    // ── Logical→physical surface ownership ───────────────────────────────
    // A physical slot owns exactly one VkImage (PhysicalSurface); logical
    // handles bind to slots, and several handles may bind the SAME slot when
    // their lifetimes never overlap (plan-driven aliasing).  Ownership is
    // therefore separated from identity: destroying a handle binding must
    // never destroy a VkImage still referenced by another handle.
    // resolve_image() is the single lookup path for every operation.
    struct PhysicalSurface {
        Image image;
        runtime::SurfaceDesc desc{};
    };
    // slot → backing image (ownership lives here, exactly once per slot)
    std::unordered_map<std::size_t, PhysicalSurface> physical_surfaces;
    // handle → slot (identity only; no ownership)
    std::unordered_map<runtime::RenderSurfaceHandle, std::size_t> surface_bindings;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    std::unordered_set<std::size_t> cuda_ready_surfaces;
    std::unordered_set<std::size_t> cuda_export_ready_surfaces;
#endif
    // Temporary native passes may release a logical handle while the active
    // frame batch is still recording commands that reference its image. Keep
    // the Vulkan binding alive until the batch has been submitted.
    std::vector<runtime::RenderSurfaceHandle> deferred_surface_releases;
    std::unordered_set<runtime::RenderSurfaceHandle> unplanned_surface_handles;
    std::size_t next_slot{0};
    // Last access kind per PHYSICAL SLOT within the current frame's
    // plan-driven batch, consumed by emit_plan_pass_barriers() to derive the
    // precise write→read / read→write / write→write memory dependencies.
    // Keyed by slot (not by logical handle) because aliased handles share
    // one image: the barrier chain must follow the image, not the identity.
    // Cleared at begin_frame_batch(); conservative batches never touch it.
    std::unordered_map<std::size_t, runtime::ResourceAccess> m_slot_last_access{};
    FrameBatchState frame_batch{};
    // Command-batch state: while active, end_frame_batch() defers the single
    // submission it would otherwise perform and keeps recording into the SAME
    // command buffer, so N overlays (N frame batches) accumulate and are
    // submitted with exactly one vkQueueSubmit at end_command_batch().
    // command_batch_started distinguishes the first frame (which opens the
    // buffer) from subsequent frames (which only flush a boundary barrier and
    // reset per-frame bookkeeping).
    bool command_batch_active{false};
    bool command_batch_started{false};
    // Frame-level GPU timing: a VkQueryPool of VK_QUERY_TYPE_TIMESTAMP with
    // two queries per frame-batch ring slot (start + end).  Null when the
    // device does not expose timestamp support.
    VkQueryPool timestamp_pool{VK_NULL_HANDLE};
    float timestamp_period_ns{0.0f};
    std::uint32_t timestamp_valid_bits{0};
    VulkanBackendStats stats{};
#include "vulkan_backend_impl_lifecycle.inc"
    ~Impl() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        for (auto& [slot, physical] : physical_surfaces) {
            (void)slot;
            destroy_image(physical.image);
        }
        destroy_image(dst);
        destroy_image(src);
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, staging_memory, nullptr);
        for (std::size_t i = 0; i < kGlyphInstanceRingSize; ++i) {
            if (glyph_instance_buffers[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, glyph_instance_buffers[i], nullptr);
            }
            if (glyph_instance_memories[i] != VK_NULL_HANDLE) {
                vkFreeMemory(device, glyph_instance_memories[i], nullptr);
            }
        }
        for (auto& slot : upload_slots) destroy_upload_slot(slot);
        for (auto& allocator : frame_batch.descriptor_allocators) {
            allocator.destroy();
        }
        for (auto& slot_fence : frame_batch.fences) {
            if (slot_fence != VK_NULL_HANDLE) vkDestroyFence(device, slot_fence, nullptr);
        }
        for (auto& slot_buffer : frame_batch.command_buffers) {
            if (slot_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot_buffer);
            }
        }
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (timeline_semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline_semaphore, nullptr);
        for (const auto id : {GpuKernelId::Composite, GpuKernelId::Transform,
                              GpuKernelId::AffineTransform, GpuKernelId::Blur,
                              GpuKernelId::ColorAdjust, GpuKernelId::Matte,
                              GpuKernelId::TextRun, GpuKernelId::FillRect}) {
            const auto handle = kernel_registry.resolve(id);
            if (handle != 0) {
                vkDestroyPipeline(device,
                    reinterpret_cast<VkPipeline>(handle), nullptr);
            }
        }
        if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (timestamp_pool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestamp_pool, nullptr);
    }

#include "vulkan_backend_impl_descriptors.inc"
#include "vulkan_backend_impl_kernels.inc"
#include "vulkan_backend_impl_surfaces.inc"
#include "vulkan_backend_impl_ops.inc"
#endif // CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_public_lifecycle.inc"
#include "vulkan_backend_public_stats.inc"

void VulkanBackend::begin_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    auto& batch = m_impl->frame_batch;
    if (batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_frame_batch: a frame batch is already active");
    }
    // Second and later frames of an active command batch keep recording into
    // the SAME command buffer (opened by the first frame).  Flush a
    // cross-overlay boundary barrier and reset only per-frame bookkeeping;
    // the descriptor allocator and command buffer stay intact so every
    // overlay's recorded descriptor sets remain valid until the single
    // submission at end_command_batch().
    if (m_impl->command_batch_active && m_impl->command_batch_started) {
        m_impl->emit_command_batch_boundary();
        batch.pass_count = 0;
        batch.sync_plan = nullptr;
        m_impl->m_slot_last_access.clear();
        batch.active = true;
        return;
    }
    const auto slot = batch.next_slot;
    // Wait ONLY on the fence of the slot being reused.  The other slots may
    // still be in flight; this is what bounds CPU-GPU overlap to the ring
    // size instead of stalling the whole device every frame.
    if (batch.in_flight[slot]) {
        const auto wait_start = profiling::now();
        const VkResult wait_result = vkWaitForFences(
            m_impl->device, 1, &batch.fences[slot], VK_TRUE, UINT64_MAX);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            spdlog::error(
                "[vulkan] DEVICE LOST REPORT phase=begin_frame_batch slot={} next_slot={} "
                "in_flight_slots=[{},{},{}] pending_timeline={} command_batch_active={}",
                slot, batch.next_slot, batch.in_flight[0], batch.in_flight[1],
                batch.in_flight[2], m_impl->pending_timeline_value,
                m_impl->command_batch_active);
        }
        check(wait_result, "vkWaitForFences(frame batch slot)");
        ++m_impl->stats.frame_slot_wait_count;
        m_impl->stats.frame_slot_wait_us +=
            static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
        check(vkResetFences(m_impl->device, 1, &batch.fences[slot]),
              "vkResetFences(frame batch slot)");
        batch.in_flight[slot] = false;
        m_impl->read_gpu_timestamps(slot);
    }
    // Every recorded pass owns a descriptor set from this slot's allocator;
    // resetting it now is safe because the slot's previous submission (the
    // only one referencing those sets) has completed.
    batch.descriptor_allocators[slot].reset();
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr};
    check(vkResetCommandBuffer(batch.command_buffers[slot], 0),
          "vkResetCommandBuffer(frame batch slot)");
    check(vkBeginCommandBuffer(batch.command_buffers[slot], &begin),
          "vkBeginCommandBuffer(frame batch slot)");
    if (m_impl->timestamp_pool != VK_NULL_HANDLE) {
        const auto query_base = static_cast<std::uint32_t>(2 * slot);
        vkCmdResetQueryPool(batch.command_buffers[slot], m_impl->timestamp_pool,
                            query_base, 2);
        vkCmdWriteTimestamp(batch.command_buffers[slot],
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_impl->timestamp_pool, query_base);
    }
    batch.active = true;
    batch.pass_count = 0;
    batch.descriptor_sets.clear();
    batch.sync_plan = nullptr;
    m_impl->m_slot_last_access.clear();
    // The first frame of a command batch opened the buffer above; mark the
    // batch as started so subsequent frames take the soft-reset path.
    if (m_impl->command_batch_active) {
        m_impl->command_batch_started = true;
    }
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::begin_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    if (m_impl->command_batch_active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a command batch is already active");
    }
    if (m_impl->frame_batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a frame batch is already active");
    }
    // The first overlay's begin_plan_batch → begin_frame_batch opens the
    // single command buffer for the whole batch.
    m_impl->command_batch_active = true;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::begin_plan_batch(const runtime::CommandPlan& plan) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    begin_frame_batch();
    m_impl->frame_batch.sync_plan = &plan.barriers;
    // Bind every planned allocation to its physical slot, backing each slot
    // with exactly one VkImage.  Lifetime-disjoint handles that share a
    // planned slot therefore alias the same device image (the registry-side
    // bind_plan_slots() propagates the same mapping for identity records).
    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle) continue;
        if (allocation.physical_slot == std::numeric_limits<std::size_t>::max()) continue;
        if (allocation.physical_slot >= plan.resources.slots.size()) continue;
        // Job-persistent surfaces (GPU asset/glyph atlases) are owned by the
        // asset cache and must never be rebound to a frame-transient planner
        // slot.  The old unconditional binding changed their lifetime to
        // FrameTransient, so end-of-job cleanup destroyed the Vulkan image
        // while the registry/cache still returned the logical handle.
        const auto existing = m_impl->surface_bindings.find(allocation.surface);
        if (existing != m_impl->surface_bindings.end()) {
            const auto physical = m_impl->physical_surfaces.find(existing->second);
            if (physical != m_impl->physical_surfaces.end() &&
                physical->second.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                continue;
            }
        }
        const auto& planned = plan.resources.slots[allocation.physical_slot];
        const runtime::SurfaceDesc desc{
            planned.width, planned.height, planned.format,
            planned.usage, runtime::LifetimeClass::FrameTransient,
            static_cast<std::size_t>(planned.width) * planned.height *
                sizeof(float) * 4};
        m_impl->bind_handle_to_slot(allocation.surface, allocation.physical_slot, desc);
    }
#else
    (void)plan;
    unsupported("begin_plan_batch");
#endif
}

void VulkanBackend::end_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    auto& batch = m_impl->frame_batch;
    if (!batch.active) return;
    if (m_impl->command_batch_active) {
        // Defer the submission: end_command_batch() performs exactly one
        // vkQueueSubmit for every overlay recorded into this command batch.
        batch.active = false;
        return;
    }
    m_impl->submit_batch();
    m_impl->flush_deferred_surface_releases();
    batch.active = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::end_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    if (!m_impl->command_batch_active) return;
    if (m_impl->command_batch_started) {
        // The final frame's end_frame_batch() deferred its submission, so the
        // single command buffer is still open and holds all N overlays.  One
        // vkQueueSubmit flushes the whole batch.
        m_impl->submit_batch();
    }
    m_impl->flush_deferred_surface_releases();
    m_impl->frame_batch.active = false;
    m_impl->command_batch_active = false;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

#include "vulkan_backend_public_surface_api.inc"
    return std::make_unique<VulkanBackend>();
}

} // namespace chronon3d::backends::vulkan
