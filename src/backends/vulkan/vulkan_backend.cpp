#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/physical_framebuffer_allocation.hpp>
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
#include "layer_batch_comp_spv.hpp"
#include "text_batch_comp_spv.hpp"
#include "text_tile_bin_comp_spv.hpp"
#include "text_tile_raster_comp_spv.hpp"
#include "memory/vulkan_memory_manager.hpp"
#include "debug/vulkan_debug_context.hpp"
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

std::vector<VulkanDeviceInfo> VulkanBackend::enumerate_devices() {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::vector<VulkanDeviceInfo> result;
    VkInstance instance = VK_NULL_HANDLE;
    const VkApplicationInfo app_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Chronon3D discovery",
        VK_MAKE_VERSION(0, 1, 0), "Chronon3D", VK_MAKE_VERSION(0, 1, 0),
        VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instance_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app_info,
        0, nullptr, 0, nullptr};
    if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
        return result;
    }
    std::uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS ||
        count == 0) {
        vkDestroyInstance(instance, nullptr);
        return result;
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return result;
    }
    for (const auto device : devices) {
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
        const bool graphics = std::any_of(
            families.begin(), families.end(), [](const auto& family) {
                return family.queueCount != 0 &&
                    (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            });
        if (!graphics) continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        std::uint64_t local_memory = 0;
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                local_memory += memory.memoryHeaps[i].size;
            }
        }
        result.push_back({
            static_cast<std::uint32_t>(result.size()), properties.deviceName,
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
            local_memory});
    }
    vkDestroyInstance(instance, nullptr);
    return result;
#else
    return {};
#endif
}

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
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_R32G32B32A32_SFLOAT};
        std::uint32_t width{0};
        std::uint32_t height{0};
        bool initialized{false};
        bool exportable{false};
        VkSemaphore cuda_to_vulkan{VK_NULL_HANDLE};
        VkSemaphore vulkan_to_cuda{VK_NULL_HANDLE};
    };

    /// Per-frame descriptor set allocator owned by VulkanDescriptorArena.
    class FrameDescriptorAllocator {
    public:
        static constexpr std::size_t kInitialChunkSets = 64;
        // Worst case per set: the layout exposes 3 storage-image bindings
        // (matte uses all three; the other kernels use two) plus one
        // storage-buffer binding (the text-run kernel's glyph instances).
        // Pool sizing over-reserves so any pass can allocate safely.
        static constexpr std::size_t kStorageImagesPerSet = 3;
        static constexpr std::size_t kStorageBuffersPerSet = 2;

        void create(VkDevice device, VkDescriptorSetLayout layout,
                    std::size_t storage_images = kStorageImagesPerSet,
                    std::size_t storage_buffers = kStorageBuffersPerSet) {
            device_ = device;
            layout_ = layout;
            storage_images_ = storage_images;
            storage_buffers_ = storage_buffers;
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
            std::vector<VkDescriptorPoolSize> pool_sizes;
            if (storage_images_ != 0) {
                pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      static_cast<std::uint32_t>(sets * storage_images_)});
            }
            if (storage_buffers_ != 0) {
                pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      static_cast<std::uint32_t>(sets * storage_buffers_)});
            }
            const VkDescriptorPoolCreateInfo pool_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
                static_cast<std::uint32_t>(sets),
                static_cast<std::uint32_t>(pool_sizes.size()), pool_sizes.data()};
            VkDescriptorPool pool = VK_NULL_HANDLE;
            check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool),
                  "vkCreateDescriptorPool(frame descriptor chunk)");
            pools_.push_back(pool);
        }

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
        std::size_t storage_images_{kStorageImagesPerSet};
        std::size_t storage_buffers_{kStorageBuffersPerSet};
        std::vector<VkDescriptorPool> pools_{};
        std::size_t active_pool_{0};
    };

    class VulkanDescriptorArena {
    public:
        std::array<FrameDescriptorAllocator, 3> pass{};
        std::array<FrameDescriptorAllocator, 3> text_tile_bin{};
        std::array<FrameDescriptorAllocator, 3> text_tile_raster{};

        void destroy() {
            for (auto& allocator : pass) allocator.destroy();
            for (auto& allocator : text_tile_bin) allocator.destroy();
            for (auto& allocator : text_tile_raster) allocator.destroy();
        }

        void reset(std::size_t slot) {
            pass[slot].reset();
            text_tile_bin[slot].reset();
            text_tile_raster[slot].reset();
        }
    };

    VulkanDescriptorArena descriptor_arena{};

    /// Frame command buffers/fences/timing are owned by the submission ring.
    struct VulkanSubmissionRing {
        static constexpr std::size_t kSlotCount = 3;
        bool active{false};
        std::size_t next_slot{0};
        std::array<VkCommandBuffer, kSlotCount> command_buffers{};
        std::array<VkFence, kSlotCount> fences{};
        std::array<bool, kSlotCount> in_flight{};
        // Descriptor pools are owned by VulkanDescriptorArena; these aliases
        // preserve the existing frame-batch access pattern during migration.
        std::array<FrameDescriptorAllocator, kSlotCount>& descriptor_allocators;
        std::array<FrameDescriptorAllocator, kSlotCount>& text_tile_bin_allocators;
        std::array<FrameDescriptorAllocator, kSlotCount>& text_tile_raster_allocators;

        VulkanSubmissionRing(VulkanDescriptorArena& arena)
            : descriptor_allocators(arena.pass),
              text_tile_bin_allocators(arena.text_tile_bin),
              text_tile_raster_allocators(arena.text_tile_raster) {}
        VulkanSubmissionRing(const VulkanSubmissionRing&) = delete;
        VulkanSubmissionRing& operator=(const VulkanSubmissionRing&) = delete;
        std::vector<VkDescriptorSet> descriptor_sets;
        std::size_t pass_count{0};
        // When set, the batch is plan-driven: ops synchronize through this
        // BarrierPlan (via begin_plan_batch) instead of the conservative
        // fallback.  pass_count doubles as the plan pass index: ops are
        // called in plan order and each op advances it by one.
        const runtime::BarrierPlan* sync_plan{nullptr};
    } submission_ring{descriptor_arena};

    using FrameBatchState = VulkanSubmissionRing;

    // ── Command-replay ring ───────────────────────────────────────────
    //
    // When the compiled program is fully recorded (fully_recorded == true),
    // prepare() records every VkCommandBuffer once.  Each frame then only
    // writes per-frame params into a persistently-mapped buffer and submits
    // the pre-recorded command buffer — zero vkCmd* calls in the hot path.
    //
    // The ring size matches the frame-batch ring so the pipeline depth
    // stays identical; the GPU can still overlap execution of N frames.
    struct ReplaySlot {
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        bool in_flight{false};
        // Persistently-mapped uniform buffer for per-frame params.
        // Written once per frame before vkQueueSubmit; the shaders read
        // from binding 0 (the legacy descriptor layout already exposes
        // storage images at 1-3, so binding 0 is free for a UBO).
        VulkanBufferAllocation params{};
    };
    static constexpr std::size_t kReplaySlotCount = 3;
    std::array<ReplaySlot, kReplaySlotCount> replay_slots{};
    bool replay_prepared{false};
    std::size_t replay_next_slot{0};

    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queue_family{0};
    VkCommandPool command_pool{VK_NULL_HANDLE};
    // ── GPU→CPU timeline calibration (VK_EXT_calibrated_timestamps) ──────
    // Anchors the device timestamp domain to the Perfetto trace-clock domain
    // so GPU work can be drawn as real bars on the "Chronon Vulkan Queue"
    // track aligned with the CPU timeline.  All fields stay defaulted when
    // the extension or timestamp queries are unavailable (fallback: CPU-side
    // submit/fence-wait events only, never fake GPU bars).
    bool calibrated_ts_supported{false};
    PFN_vkGetCalibratedTimestampsEXT pfn_get_calibrated_timestamps{nullptr};
    bool gpu_timestamps_calibrated{false};
    std::uint64_t calibration_gpu_ts{0};
    std::int64_t calibration_cpu_trace_ns{0};
    std::uint64_t calibration_max_deviation{0};
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore timeline_semaphore{VK_NULL_HANDLE};
    std::uint64_t next_timeline_value{0};
    std::uint64_t pending_timeline_value{0};
    VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorSetLayout text_tile_bin_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorSetLayout text_tile_raster_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, 3> glow_descriptor_sets{};
    class VulkanKernelStore {
    public:
        GpuKernelRegistry registry{};
        VkPipelineLayout general_layout{VK_NULL_HANDLE};
        VkPipelineLayout text_tile_bin_layout{VK_NULL_HANDLE};
        VkPipelineLayout text_tile_raster_layout{VK_NULL_HANDLE};

        void destroy(VkDevice device) noexcept {
            for (const auto id : {GpuKernelId::Composite, GpuKernelId::Transform,
                                  GpuKernelId::AffineTransform, GpuKernelId::Blur,
                                  GpuKernelId::ColorAdjust, GpuKernelId::Matte,
                                  GpuKernelId::TextRun, GpuKernelId::FillRect,
                                  GpuKernelId::LayerBatch, GpuKernelId::TextBatch,
                                  GpuKernelId::TextTileBin, GpuKernelId::TextTileRaster}) {
                const auto handle = registry.resolve(id);
                if (handle != 0) vkDestroyPipeline(device, reinterpret_cast<VkPipeline>(handle), nullptr);
            }
            registry = {};
            if (general_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, general_layout, nullptr);
            if (text_tile_bin_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, text_tile_bin_layout, nullptr);
            if (text_tile_raster_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, text_tile_raster_layout, nullptr);
            general_layout = VK_NULL_HANDLE;
            text_tile_bin_layout = VK_NULL_HANDLE;
            text_tile_raster_layout = VK_NULL_HANDLE;
        }
    } kernels;
    VulkanBufferAllocation staging{};
    // Device-local storage buffers for glyph instances. A frame can contain
    // multiple TextRun passes (watermark + subtitles); each pass gets its own
    // buffer so one in-command-buffer update can never overwrite data read by
    // an earlier dispatch. The pass ring is deliberately generous and is
    // recycled only with the owning frame-batch slot.
    static constexpr std::size_t kGlyphInstancePassesPerSlot = 64;
    static constexpr std::size_t kGlyphInstanceRingSize =
        FrameBatchState::kSlotCount * kGlyphInstancePassesPerSlot;
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> glyph_instance_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> glyph_instance_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> glyph_instance_sizes{};
    // Device-local storage buffers for layer batch instances
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> layer_instance_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> layer_instance_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> layer_instance_sizes{};
    // Device-local storage buffers for text batch dynamic runs
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_run_dynamic_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> text_run_dynamic_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> text_run_dynamic_sizes{};
    // Per-pass tile binning buffers. Counts are reset with vkCmdFillBuffer;
    // both buffers stay device-local and are reused with the frame ring.
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_tile_count_buffers{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_tile_index_buffers{};
    class VulkanUploadRing {
    public:
        struct UploadSlot {
            VulkanBufferAllocation buffer_allocation{};
            VkCommandBuffer command_buffer{VK_NULL_HANDLE};
            VkFence fence{VK_NULL_HANDLE};
            std::uint64_t ticket{0};
            bool in_flight{false};
        };
        static constexpr std::size_t kSlotCount = 3;
        std::array<UploadSlot, kSlotCount> slots{};
        std::size_t next_slot{0};
    } uploads;
    Image dst{};
    Image src{};

    // Surface identity, ownership, and lifecycle are isolated in the concrete
    // store below. Other Impl responsibilities access it through the same
    // member, preserving the existing private implementation boundary.
    class VulkanSurfaceStore {
    public:
        struct PhysicalSurface {
            Image image;
            runtime::SurfaceDesc desc{};
        };

        // The store owns all logical-to-physical surface state. Its concrete
        // type keeps surface ownership separate from backend orchestration.
        std::unordered_map<std::size_t, PhysicalSurface> physical_surfaces;
        std::unordered_map<runtime::RenderSurfaceHandle, std::size_t> surface_bindings;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        std::unordered_set<std::size_t> cuda_ready_surfaces;
        std::unordered_set<std::size_t> cuda_export_ready_surfaces;
#endif
        std::vector<runtime::RenderSurfaceHandle> deferred_surface_releases;
        std::unordered_set<runtime::RenderSurfaceHandle> unplanned_surface_handles;
        std::size_t next_slot{0};
        std::unordered_map<std::size_t, runtime::ResourceAccess> slot_last_access{};

        [[nodiscard]] bool valid(runtime::RenderSurfaceHandle handle) const noexcept {
            return surface_bindings.contains(handle);
        }

        [[nodiscard]] std::size_t physical_count() const noexcept {
            return physical_surfaces.size();
        }

        void clear_access_state() noexcept { slot_last_access.clear(); }

        [[nodiscard]] bool is_job_persistent(
            runtime::RenderSurfaceHandle handle) const noexcept {
            const auto binding = surface_bindings.find(handle);
            if (binding == surface_bindings.end()) return false;
            const auto physical = physical_surfaces.find(binding->second);
            return physical != physical_surfaces.end() &&
                   physical->second.desc.lifetime == runtime::LifetimeClass::JobPersistent;
        }

        Image& bind(runtime::RenderSurfaceHandle handle,
                    std::size_t slot,
                    const runtime::SurfaceDesc& desc) {
            const auto previous = surface_bindings.find(handle);
            if (previous != surface_bindings.end() && previous->second != slot) {
                const auto old_slot = previous->second;
                const auto old_it = physical_surfaces.find(old_slot);
                const bool pinned = old_it != physical_surfaces.end() &&
                                    old_it->second.image.initialized;
                if (pinned) slot = old_slot;
                else surface_bindings.erase(previous);
            }
            if (physical_surfaces.contains(slot) &&
                physical_surfaces.at(slot).image.initialized) {
                for (const auto& [bound_handle, bound_slot] : surface_bindings) {
                    if (bound_handle != handle && bound_slot == slot) {
                        slot = next_slot++;
                        break;
                    }
                }
            }
            return bind_handle_to_slot_impl(handle, slot, desc);
        }

        void prune_unused_slots() { prune_unused_slots_impl(); }

    private:
        Image& bind_handle_to_slot_impl(runtime::RenderSurfaceHandle handle,
                                        std::size_t slot,
                                        const runtime::SurfaceDesc& desc) {
            auto& physical = physical_surfaces[slot];
            if (physical.image.image != VK_NULL_HANDLE && owner_->plan_preallocated) {
                if (physical.image.width != desc.width || physical.image.height != desc.height) {
                    throw std::invalid_argument("Vulkan preallocated surface dimensions mismatch");
                }
            }
            // Binding a compiled allocation is metadata-only.  Image
            // materialization is performed by ensure_surface() on first use,
            // allowing unused interval slots to remain unallocated.
            physical.desc = desc;
            surface_bindings[handle] = slot;
            owner_->stats.physical_surfaces_peak = std::max(
                owner_->stats.physical_surfaces_peak,
                static_cast<std::uint64_t>(physical_surfaces.size()));
            return physical.image;
        }

        void prune_unused_slots_impl() {
            for (auto it = physical_surfaces.begin(); it != physical_surfaces.end();) {
                bool in_use = false;
                for (const auto& [handle, slot] : surface_bindings) {
                    (void)handle;
                    if (slot == it->first) { in_use = true; break; }
                }
                if (in_use || it->second.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                    ++it;
                    continue;
                }
                owner_->destroy_image(it->second.image);
                it = physical_surfaces.erase(it);
            }
        }

    public:
        VulkanBackend::Impl* owner_{nullptr};

    public:
        // Final image ownership remains in the store. Impl supplies only
        // the existing Vulkan destruction hook and retains backend teardown
        // ordering for all non-surface resources.
        void destroy_all(VulkanBackend::Impl& owner) noexcept;
    } surfaces;

    // Surface-store façade keeps backend call sites independent from the
    // store's maps while preserving the existing private implementation.
    void destroy_surface_images() noexcept {
        surfaces.destroy_all(*this);
    }

    void defer_surface_release(runtime::RenderSurfaceHandle handle) {
        surfaces.deferred_surface_releases.push_back(handle);
    }

    [[nodiscard]] bool surface_valid(runtime::RenderSurfaceHandle handle) const noexcept {
        return surfaces.valid(handle);
    }

    [[nodiscard]] std::size_t surface_physical_count() const noexcept {
        return surfaces.physical_count();
    }

    void clear_surface_access_state() noexcept {
        surfaces.clear_access_state();
    }

    [[nodiscard]] bool surface_is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
        return surfaces.is_job_persistent(handle);
    }

    Image& bind_surface_to_slot(runtime::RenderSurfaceHandle handle,
                                std::size_t slot,
                                const runtime::SurfaceDesc& desc) {
        return surfaces.bind(handle, slot, desc);
    }

    void prune_surface_slots() {
        surfaces.prune_unused_slots();
    }
    // Compatibility alias; ownership is held by VulkanSubmissionRing.
    VulkanSubmissionRing& frame_batch;

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
    VulkanMemoryManager memory_manager{};
    VulkanDebugContext* debug_context{nullptr};
    VulkanBackendStats stats{};
#include "vulkan_backend_lifecycle_private.cpp"
    ~Impl() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        surfaces.destroy_all(*this);
        destroy_image(dst);
        destroy_image(src);
        if (staging.buffer != VK_NULL_HANDLE) memory_manager.destroy_buffer(staging);
        for (std::size_t i = 0; i < kGlyphInstanceRingSize; ++i) {
            if (glyph_instance_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(glyph_instance_buffers[i]);
            }
            if (layer_instance_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(layer_instance_buffers[i]);
            }
            if (text_run_dynamic_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_run_dynamic_buffers[i]);
            }
            if (text_tile_count_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_tile_count_buffers[i]);
            }
            if (text_tile_index_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_tile_index_buffers[i]);
            }
        }
        for (auto& slot : uploads.slots) destroy_upload_slot(slot);
        descriptor_arena.destroy();
        for (auto& slot_fence : frame_batch.fences) {
            if (slot_fence != VK_NULL_HANDLE) vkDestroyFence(device, slot_fence, nullptr);
        }
        for (auto& slot_buffer : frame_batch.command_buffers) {
            if (slot_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot_buffer);
            }
        }
        // ── Replay slot teardown ───────────────────────────────────
        for (auto& slot : replay_slots) {
            if (slot.command_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
            }
            if (slot.fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, slot.fence, nullptr);
            }
            if (slot.params.buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(slot.params);
            }
        }
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (timeline_semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline_semaphore, nullptr);
        kernels.destroy(device);
        // Descriptor pools are destroyed by descriptor_arena.destroy().
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (text_tile_bin_descriptor_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_tile_bin_descriptor_layout, nullptr);
        }
        if (text_tile_raster_descriptor_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_tile_raster_descriptor_layout, nullptr);
        }
        if (timestamp_pool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestamp_pool, nullptr);
    }

#include "vulkan_descriptor_arena_private.cpp"
#include "vulkan_surface_store_private.cpp"
#include "vulkan_kernel_store_private.cpp"
#include "vulkan_backend_operations_private.cpp"

void VulkanBackend::Impl::VulkanSurfaceStore::destroy_all(
    VulkanBackend::Impl& owner) noexcept {
    // The store is the sole owner of physical surface images. Impl remains
    // responsible for the surrounding backend resources and invokes this
    // first, after the device idle point, preserving the original teardown
    // order for staging, rings, descriptors and pipelines.
    for (auto& [slot, physical] : physical_surfaces) {
        (void)slot;
        owner.destroy_image(physical.image);
    }
    physical_surfaces.clear();
    surface_bindings.clear();
    deferred_surface_releases.clear();
    unplanned_surface_handles.clear();
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    cuda_ready_surfaces.clear();
    cuda_export_ready_surfaces.clear();
#endif
    slot_last_access.clear();
    next_slot = 0;
}
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
        m_impl->clear_surface_access_state();
        batch.active = true;
        return;
    }
    const auto slot = batch.next_slot;
    // Wait ONLY on the fence of the slot being reused.  The other slots may
    // still be in flight; this is what bounds CPU-GPU overlap to the ring
    // size instead of stalling the whole device every frame.
    if (batch.in_flight[slot]) {
        const auto wait_start = profiling::now();
        // CPU-side fence wait — the honest fallback for GPU timing when
        // calibrated timestamps are unavailable (Fase 6): the wait shows on
        // the render thread track, no fake GPU bar is drawn.
        CHRONON_TRACE_SCOPE("chronon.gpu", "FenceWait");
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
    m_impl->descriptor_arena.reset(slot);
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
    m_impl->clear_surface_access_state();
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
    // A previous frame may have kept its logical transient handles alive
    // until the encoder package released them.  begin_frame_batch() has just
    // waited for the ring slot that is about to be reused, so opportunistically
    // retire completed bindings now, before this plan adds the next frame's
    // aliases.  Without this boundary, plan-bound handles accumulated in the
    // surface store and every CPU fallback allocated another full-size image
    // until Vulkan ran out of device memory.
    m_impl->retire_completed_frame_transient_surfaces();
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
        if (m_impl->surface_is_job_persistent(allocation.surface)) continue;
        const auto& planned = plan.resources.slots[allocation.physical_slot];
        // Physical slots are aliases, not descriptions of every logical
        // resource assigned to them.  The slot table can legitimately carry
        // the canvas-sized fallback dimensions, while a request is a tight
        // producer surface (text/overlay).  Using the slot dimensions here
        // promoted every aliased resource to a full 1920x1080 image and made
        // long Vulkan exports exhaust device memory.  Bind with the logical
        // request's real dimensions; the slot remains the alias identity.
        runtime::ResourceDesc request_desc{};
        if (allocation.request_index < plan.resources.requests.size()) {
            request_desc = plan.resources.requests[allocation.request_index].desc;
        }
        const auto width = request_desc.width != 0 ? request_desc.width : planned.width;
        const auto height = request_desc.height != 0 ? request_desc.height : planned.height;
        const auto format = request_desc.format != runtime::PixelFormat::Unknown
            ? request_desc.format : planned.format;
        const auto usage = request_desc.usage != runtime::ResourceUsage::Generic
            ? request_desc.usage : planned.usage;
        const runtime::SurfaceDesc desc{
            width, height, format,
            usage, runtime::LifetimeClass::FrameTransient,
            static_cast<std::size_t>(width) * height *
                sizeof(float) * 4};
        m_impl->bind_surface_to_slot(allocation.surface, allocation.physical_slot, desc);
    }
    // Rebinding a plan can leave the pre-plan pool slots orphaned. They are
    // not part of the compiled plan and must not inflate the physical-surface
    // pool or survive the frame as hidden compatibility allocations.
    m_impl->prune_surface_slots();
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
    m_impl->submit_batch();        m_impl->flush_deferred_surface_releases();
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
    }        m_impl->flush_deferred_surface_releases();
    m_impl->frame_batch.active = false;
    m_impl->command_batch_active = false;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

// ── Phase 8: command-replay public API ────────────────────────────────

std::size_t VulkanBackend::replay_slot_count() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return Impl::kReplaySlotCount;
#else
    return 0;
#endif
}

#ifdef CHRONON3D_ENABLE_VULKAN
VkCommandBuffer VulkanBackend::begin_replay_recording(std::size_t slot_index) {
    std::lock_guard lock(m_impl->api_mutex);
    return m_impl->begin_replay_recording(slot_index);
}
#endif

void VulkanBackend::end_replay_recording(std::size_t slot_index) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->end_replay_recording(slot_index);
#else
    (void)slot_index;
#endif
}

void VulkanBackend::replay_submit(std::size_t slot_index,
                                   const void* params, std::size_t params_size) {
#ifdef CHRONON3D_ENABLE_VULKAN
    std::lock_guard lock(m_impl->api_mutex);
    m_impl->replay_submit(slot_index, params,
                          static_cast<VkDeviceSize>(params_size));
#else
    (void)slot_index;
    (void)params;
    (void)params_size;
#endif
}

#include "vulkan_backend_public_surface_api.inc"
    return std::make_unique<VulkanBackend>(device_index);
}

} // namespace chronon3d::backends::vulkan
