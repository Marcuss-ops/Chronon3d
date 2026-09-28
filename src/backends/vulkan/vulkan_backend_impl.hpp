#pragma once

// ============================================================================
// vulkan_backend_impl.hpp — PRIVATE header for VulkanBackend::Impl.
// ============================================================================

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/tracing/tracing.hpp>
#include <chronon3d/runtime/gpu_device_lost_error.hpp>
#include "memory/vulkan_memory_manager.hpp"
#include "debug/vulkan_debug_context.hpp"
#include "vulkan_descriptor_authority.hpp"
#include "vulkan_kernel_store.hpp"
#include "vulkan_submission_authority.hpp"
#include "vulkan_surface_authority.hpp"
#include "vulkan_upload_authority.hpp"
#include "vulkan_stats_authority.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <cstring>
#include <initializer_list>
#include <iterator>
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

struct alignas(16) VulkanGpuGlyphStatic {
    std::uint32_t run_index{0};
    std::uint32_t atlas_page_and_flags{0};
    std::uint32_t atlas_pos{0};
    std::uint32_t atlas_size{0};
    float plane_left{0.0f};
    float plane_top{0.0f};
    float plane_right{0.0f};
    float plane_bottom{0.0f};
    std::uint32_t draw_order{0};
    std::uint32_t pad0{0};
    float origin_x{0.0f};
    float origin_y{0.0f};
};
static_assert(sizeof(VulkanGpuGlyphStatic) == 48,
              "VulkanGpuGlyphStatic must match the tiled text std430 ABI");

// Selector and tracking values are derived from the animated run each frame.
// Keep them out of the static glyph record so the static cache remains valid
// across frame samples.
struct VulkanGpuGlyphDynamic {
    float tracking_prefix{0.0f};
    float selector_weight{1.0f};
};
static_assert(sizeof(VulkanGpuGlyphDynamic) == 8,
              "VulkanGpuGlyphDynamic must match the tiled text std430 ABI");

struct VulkanBackend::Impl {
    // Device lifetime remains owned by Impl. Surface state and submission
    // state have their own locks in VulkanSurfaceAuthority and
    // VulkanSubmissionAuthority; there is deliberately no backend-wide data
    // plane mutex here. Destruction still assumes the backend lifetime has
    // ended before the Impl is torn down.

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
        bool cuda_timeline{false};
        std::shared_ptr<InteropGeneration> interop_generation{};
        VkSemaphore cuda_to_vulkan{VK_NULL_HANDLE};
        VkSemaphore vulkan_to_cuda{VK_NULL_HANDLE};
        std::uint64_t last_vulkan_to_cuda_value{0};
        std::optional<std::size_t> cuda_export_fence_slot{};
        bool cuda_export_standalone_fence{false};
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
    std::array<VkDescriptorSet, 6>& glow_descriptor_sets{
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
    VulkanStatsAuthority stats{};
    std::uint64_t active_frame_index{0};
    std::vector<VulkanFrameGpuTiming>& completed_frame_timings{
        stats.completed_frame_timings};
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
    // High-water-mark scratch. Large text batches may grow these once, but
    // never allocate a temporary spill vector on every draw call.
    std::vector<VulkanGpuGlyphStatic> text_glyph_scratch{};
    std::vector<VulkanGpuGlyphDynamic> text_glyph_dynamic_scratch{};
    std::vector<runtime::TextRunDynamic> text_run_scratch{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> glyph_instance_sizes{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> layer_instance_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> layer_instance_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> layer_instance_sizes{};
    static constexpr std::size_t kPathGeometryPassesPerSlot = 256;
    static constexpr std::size_t kPathGeometryRingSize =
        FrameBatchState::kSlotCount * kPathGeometryPassesPerSlot + 1U;
    std::array<VulkanBufferAllocation, kPathGeometryRingSize> path_geometry_buffers{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_run_dynamic_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> text_run_dynamic_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> text_run_dynamic_sizes{};
    std::array<VulkanBufferAllocation, kGlyphInstanceRingSize> text_glyph_dynamic_buffers{};
    std::array<std::uint64_t, kGlyphInstanceRingSize> text_glyph_dynamic_hashes{};
    std::array<VkDeviceSize, kGlyphInstanceRingSize> text_glyph_dynamic_sizes{};
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

    // DEMOLISHED (P1.4): plan_preallocated/plan_canvas_width/plan_canvas_height
    // removed — plan_preallocated was initialized false and only ever written
    // false; the canvas extents were written but never read.

    void destroy_surface_images() noexcept { surfaces.destroy_all(*this); }
    void defer_surface_release(runtime::RenderSurfaceHandle handle) {
        ++stats.deferred_surface_release_count;
        const std::size_t slot = frame_batch.next_slot;
        surfaces.deferred_surface_releases.push_back({handle, slot});
    }
    [[nodiscard]] bool surface_valid(runtime::RenderSurfaceHandle handle) const noexcept {
        return surfaces.valid(handle);
    }
    [[nodiscard]] std::size_t surface_physical_count() const noexcept {
        return surfaces.physical_count();
    }
    [[nodiscard]] bool surface_is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
        return surfaces.is_job_persistent(handle);
    }
    Image& bind_surface_to_slot(runtime::RenderSurfaceHandle handle,
                                std::size_t slot,
                                const runtime::SurfaceDesc& desc,
                                std::span<const runtime::RenderSurfaceHandle>
                                    planned_occupants = {}) {
        return surfaces.bind(handle, slot, desc, planned_occupants);
    }
    void prune_surface_slots() { surfaces.prune_unused_slots(); }
    void reclaim_unused_transient_surface_slots() {
        surfaces.reclaim_unused_transient_slots();
    }

    // ── Recording leases (audit N4) ──────────────────────────────────────
    // The pin table is a leaf: it is written while the store lock is held and
    // read while the submission lock is held, and it never acquires either.
    // `second` is a reference count so a batch-wide plan pin and the per-op
    // pins of individual passes can coexist.
    using RecordingLease = decltype(surfaces)::SurfaceLease;

    void pin_recording_leases(
        std::span<const runtime::RenderSurfaceHandle> handles) {
        // Surface domain only: this is the whole point of the N4 decoupling —
        // the recording path never takes it together with the submission ring.
        DomainLock store_lock(*this, LockDomain::Surface);
        pin_recording_leases_locked(handles);
    }

    /// Which contention domain(s) a lock site takes. `Dual` is the coupling
    /// the audit N4 measurement is about: two independent domains serialized
    /// by one acquisition.
    enum class LockDomain : std::uint8_t { Surface, Submission, Dual };

    /// Audit N4 measurement: the contention domains, timed.
    ///
    /// Drop-in replacement for the `std::scoped_lock(surfaces.mutex,
    /// submission_ring.mutex)` it displaces — same fixed acquisition order
    /// (surfaces → submission), same single-owner scope, no lambda and no
    /// control-flow change at the call site — plus the wait/hold accounting
    /// the audit asked for. Only sites that genuinely need both use `Dual`;
    /// the recording path is not one of them any more.
    class DomainLock {
    public:
        explicit DomainLock(Impl& impl, LockDomain domain)
            : impl_(impl), domain_(domain), wait_start_(profiling::now()),
              // Fixed acquisition order (surfaces → submission), the order the
              // displaced scoped_lock used, so no inversion is introduced.
              first_(domain == LockDomain::Submission ? impl.submission_ring.mutex
                                                      : impl.surfaces.mutex),
              second_(impl.submission_ring.mutex, std::defer_lock) {
            if (domain == LockDomain::Dual) second_.lock();
            impl_.record_lock_wait(domain_, static_cast<std::uint64_t>(
                profiling::elapsed_us(wait_start_)));
            hold_start_ = profiling::now();
        }

        ~DomainLock() {
            impl_.record_lock_hold(domain_, static_cast<std::uint64_t>(
                profiling::elapsed_us(hold_start_)));
        }

        DomainLock(const DomainLock&) = delete;
        DomainLock& operator=(const DomainLock&) = delete;
        DomainLock(DomainLock&&) = delete;
        DomainLock& operator=(DomainLock&&) = delete;

    private:
        Impl& impl_;
        LockDomain domain_;
        std::chrono::steady_clock::time_point wait_start_{};
        std::unique_lock<std::mutex> first_;
        // Owns nothing unless the domain is Dual (defer_lock).
        std::unique_lock<std::mutex> second_;
        std::chrono::steady_clock::time_point hold_start_{};
    };

    void record_lock_wait(LockDomain domain, std::uint64_t microseconds) noexcept {
        switch (domain) {
            case LockDomain::Surface:
                ++stats.surface_lock_acquires;
                stats.surface_lock_wait_us += microseconds;
                return;
            case LockDomain::Submission:
                ++stats.submission_lock_acquires;
                stats.submission_lock_wait_us += microseconds;
                return;
            case LockDomain::Dual:
                ++stats.dual_lock_acquires;
                stats.dual_lock_wait_us += microseconds;
                return;
        }
    }

    void record_lock_hold(LockDomain domain, std::uint64_t microseconds) noexcept {
        switch (domain) {
            case LockDomain::Surface:
                stats.surface_lock_hold_us += microseconds;
                return;
            case LockDomain::Submission:
                stats.submission_lock_hold_us += microseconds;
                return;
            case LockDomain::Dual:
                stats.dual_lock_hold_us += microseconds;
                return;
        }
    }

    /// Recording mutex MUST be held. Reverse the first `count` valid entries of
    /// `handles` — i.e. drop exactly the references a failed
    /// pin_recording_leases_locked() managed to take, and no more.
    ///
    /// The count is what makes this safe on a list with repeated handles: a
    /// blind release of the whole list could decrement a reference that belongs
    /// to an enclosing plan batch and unpin a slot it still records into.
    void rollback_pins_locked(
        std::span<const runtime::RenderSurfaceHandle> handles,
        std::size_t count) {
        std::size_t undone = 0;
        for (const auto handle : handles) {
            if (undone == count) break;
            if (handle == runtime::kInvalidRenderSurfaceHandle) continue;
            release_lease_entry_locked(handle);
            ++undone;
        }
    }

    /// Store lock MUST be held (the batch transitions already hold it).
    ///
    /// Pin/unpin are list-symmetric: every element of `handles` contributes
    /// exactly one reference count, so a list that repeats a handle (e.g.
    /// composite(h, h)) is balanced by releasing the same list.
    void pin_recording_leases_locked(
        std::span<const runtime::RenderSurfaceHandle> handles) {
        std::lock_guard pin_lock(recording_leases_mutex);
        std::size_t processed = 0;
        try {
            for (const auto handle : handles) {
                if (handle == runtime::kInvalidRenderSurfaceHandle) continue;
                auto it = recording_leases.find(handle);
                if (it == recording_leases.end()) {
                    it = recording_leases
                             .emplace(handle, std::make_pair(
                                                  surfaces.acquire_lease(handle),
                                                  std::size_t{0}))
                             .first;
                }
                ++it->second.second;
                ++processed;
            }
        } catch (...) {
            // A handle whose lease cannot be taken must not leave the earlier
            // pins of this call behind, or prune and re-materialization would be
            // wedged forever. The unwind loop lives in rollback_pins_locked()
            // so this error path carries no loop of its own.
            rollback_pins_locked(handles, processed);
            recording_leases_active.store(!recording_leases.empty(),
                                          std::memory_order_release);
            throw;
        }
        recording_leases_active.store(!recording_leases.empty(),
                                      std::memory_order_release);
    }

    void release_recording_leases(
        std::span<const runtime::RenderSurfaceHandle> handles) {
        DomainLock store_lock(*this, LockDomain::Surface);
        release_recording_leases_locked(handles);
    }

    /// Store lock MUST be held. Symmetric counterpart of
    /// pin_recording_leases_locked(handles): one release per list element.
    void release_recording_leases_locked(
        std::span<const runtime::RenderSurfaceHandle> handles) {
        std::lock_guard pin_lock(recording_leases_mutex);
        for (const auto handle : handles) {
            if (handle == runtime::kInvalidRenderSurfaceHandle) continue;
            release_lease_entry_locked(handle);
        }
        recording_leases_active.store(!recording_leases.empty(),
                                      std::memory_order_release);
    }

    /// recording_leases_mutex MUST be held.
    void release_lease_entry_locked(runtime::RenderSurfaceHandle handle) {
        const auto it = recording_leases.find(handle);
        if (it == recording_leases.end()) return;
        if (it->second.second > 1) {
            --it->second.second;
            return;
        }
        surfaces.release_lease(it->second.first);
        recording_leases.erase(it);
    }

    void release_all_recording_leases() {
        DomainLock store_lock(*this, LockDomain::Surface);
        release_all_recording_leases_locked();
    }

    /// Store lock MUST be held. Drops one reference from every entry: used by
    /// the batch end to release the plan-wide lease set.
    void release_all_recording_leases_locked() {
        std::lock_guard pin_lock(recording_leases_mutex);
        for (auto it = recording_leases.begin();
             it != recording_leases.end();) {
            if (it->second.second > 1) {
                --it->second.second;
                ++it;
                continue;
            }
            surfaces.release_lease(it->second.first);
            it = recording_leases.erase(it);
        }
        recording_leases_active.store(!recording_leases.empty(),
                                      std::memory_order_release);
    }

    /// True while THIS thread is inside a recording fn() whose surface-store
    /// lock has been released, so resolve_image() must read the lease table
    /// instead of the store map.
    ///
    /// Thread-local on purpose: the decision belongs to one recording op, not
    /// to the backend. Another thread parked in download_surface() still holds
    /// the store lock and must keep reading the store even while plan-wide
    /// leases are pinned.
    [[nodiscard]] static bool& recording_resolve_context() noexcept {
        static thread_local bool value = false;
        return value;
    }

    /// RAII flag for the recording window; nests by saving the previous value.
    struct RecordingResolveScope {
        const bool previous;
        RecordingResolveScope() noexcept : previous(recording_resolve_context()) {
            recording_resolve_context() = true;
        }
        ~RecordingResolveScope() { recording_resolve_context() = previous; }
        RecordingResolveScope(const RecordingResolveScope&) = delete;
        RecordingResolveScope& operator=(const RecordingResolveScope&) = delete;
    };

    /// Lock-free lease lookup used by resolve_image() while recording.
    [[nodiscard]] Image* leased_image(runtime::RenderSurfaceHandle handle) {
        if (!recording_leases_active.load(std::memory_order_acquire)) {
            return nullptr;
        }
        std::lock_guard pin_lock(recording_leases_mutex);
        const auto it = recording_leases.find(handle);
        return it == recording_leases.end() ? nullptr : it->second.first.image;
    }

    /// Generation-token revalidation, run between the state lookup and the
    /// ring registration. False means a leased slot changed underneath the
    /// op — a pin-enforcement bug that must fail loudly, never record on.
    [[nodiscard]] bool revalidate_recording_leases() const {
        std::lock_guard pin_lock(recording_leases_mutex);
        for (const auto& entry : recording_leases) {
            if (!surfaces.revalidate(entry.second.first)) return false;
        }
        return true;
    }

    VulkanSubmissionRing& frame_batch;
    bool& command_batch_active{submission_ring.command_batch_active};
    bool& command_batch_started{submission_ring.command_batch_started};
    float timestamp_period_ns{0.0f};
    std::uint32_t timestamp_valid_bits{0};
    VulkanMemoryManager memory_manager{};
    VulkanDebugContext* debug_context{nullptr};
    // Scratch reused by the frame-transient cleanup drains so per-frame
    // release_frame_transient_surfaces()/retire_completed_frame_transient_surfaces()
    // do not re-allocate a handle vector on every call. Impl methods are
    // serialized on the backend instance (single queue/submission thread), so
    // a single member is safe; contents are cleared before each fill.
    std::vector<runtime::RenderSurfaceHandle> transient_release_scratch_;
    // Reused scratch for the plan-wide lease set (one fill per plan batch, so
    // the compiled hot path stays allocation-free after warmup).
    std::vector<runtime::RenderSurfaceHandle> plan_lease_scratch_{};

    // Recording pin table (audit N4). Guarded by recording_leases_mutex, which
    // is always the innermost lock of any acquisition chain.
    std::unordered_map<runtime::RenderSurfaceHandle,
                       std::pair<RecordingLease, std::size_t>>
        recording_leases{};
    mutable std::mutex recording_leases_mutex{};
    std::atomic<bool> recording_leases_active{false};

    // HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — submit() scratch.
    // Rebuilt as four fresh std::vector allocations on EVERY submit() (once
    // per frame, plus CUDA-interop wait-slot entries). Reused here instead:
    // same single-submission-thread serialization contract as
    // transient_release_scratch_ above; contents are cleared before each
    // fill and capacity is retained across frames.
    std::vector<VkSemaphore> submit_wait_semaphores_;
    std::vector<VkPipelineStageFlags> submit_wait_stages_;
    std::vector<VkSemaphore> submit_signal_semaphores_;
    std::vector<std::uint64_t> submit_wait_values_;
    std::vector<std::uint64_t> submit_signal_values_;

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
    VkSemaphore make_external_timeline_semaphore();
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
    void write_dashed_path_descriptors(VkDescriptorSet set, const Image& destination,
                                       VkBuffer geometry_buffer);
    void bind_fill_rect_descriptors(const Image& destination);
    void write_matte_descriptors(VkDescriptorSet set, const Image& destination,
                                 const Image& target, const Image& matte);
    void write_text_run_descriptors(VkDescriptorSet set, const Image& destination,
                                    const Image& atlas, VkBuffer instance_buffer);
    void write_layer_batch_descriptors(VkDescriptorSet set, const Image& destination,
                                       const Image& source, VkBuffer instance_buffer);
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
        VkBuffer glyph_buffer, VkBuffer glyph_dynamic_buffer, VkBuffer run_buffer,
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

    // DEMOLISHED (P1.4): preallocate_plan_surfaces removed — the surface
    // authority materializes lazily from the compiled plan.
    bool slot_in_use(std::size_t slot) const;
    void prune_unused_transient_slots(
        std::size_t max_retained = VulkanSurfaceAuthority<Image, Impl>::kMaxRetainedTransientSlots);
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
    void record_slot_transient_surface(std::size_t slot,
                                       runtime::RenderSurfaceHandle handle);
    void retire_frame_slot_transient_surfaces(std::size_t slot);
    // Shared frame-transient drain used by both cleanup paths; the caller
    // performs its own pre-drain (blocking wait vs non-blocking fence poll).
    void release_frame_transient_handles(
        bool log_lifecycle,
        std::span<const runtime::RenderSurfaceHandle> protected_handles = {});
    void release_frame_transient_surfaces() noexcept;
    void retire_completed_frame_transient_surfaces(
        std::span<const runtime::RenderSurfaceHandle> protected_handles = {}) noexcept;
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
    std::uint64_t upload_rows(runtime::RenderSurfaceHandle handle,
                              const runtime::SurfaceDesc& desc,
                              const raster::BBox& clip,
                              const graph::RenderBackend::UploadRows& rows,
                              bool wait_for_completion);
    void clear_uninitialized_upload_target(VkCommandBuffer command,
                                           Image& image);
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
    void record_bloom_threshold(VkCommandBuffer command, VkDescriptorSet descriptors,
                                const Image& destination, const Image& source,
                                float threshold);
    void record_color_adjust(VkCommandBuffer command, VkDescriptorSet descriptors,
                             const Image& destination, const Image& source,
                             float brightness, float contrast,
                             const Color& tint, float tint_amount);
    void record_lighting(VkCommandBuffer command, VkDescriptorSet descriptors,
                         const Image& destination, const Image& source,
                         const Color& multiplier);
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
                          const Color& color,
                          const std::array<Vec2, 8>& vertices = {});
    void fill_dashed_path(runtime::RenderSurfaceHandle destination,
                          std::int32_t x0, std::int32_t y0,
                          std::int32_t x1, std::int32_t y1,
                          float width, const Color& color,
                          std::span<const Vec2> points,
                          std::span<const std::uint32_t> run_lengths,
                          bool has_fill, std::uint32_t fill_vertex_count,
                          const std::array<Vec2, 8>& fill_vertices);
    void fill_path(runtime::RenderSurfaceHandle destination,
                   std::span<const Vec2> vertices, const Color& color);
    void fill_grid(runtime::RenderSurfaceHandle destination,
                   std::int32_t x0, std::int32_t y0,
                   std::int32_t x1, std::int32_t y1,
                   const Color& line_color,
                   const Vec4& shape, const Vec4& line,
                   const std::array<Vec2, 8>& vertices);
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
    void bloom(runtime::RenderSurfaceHandle destination,
               runtime::RenderSurfaceHandle source,
               runtime::RenderSurfaceHandle scratch_horizontal,
               runtime::RenderSurfaceHandle scratch_vertical,
               float threshold, float radius, float intensity);
    void color_adjust(runtime::RenderSurfaceHandle destination,
                      runtime::RenderSurfaceHandle source,
                      float brightness, float contrast,
                      const Color& tint, float tint_amount);
    void lighting(runtime::RenderSurfaceHandle destination,
                  runtime::RenderSurfaceHandle source,
                  const Color& multiplier);
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
    void ensure_path_geometry_buffer(VkDeviceSize bytes, std::size_t index);
    void ensure_text_run_dynamic_buffer(VkDeviceSize bytes, std::size_t index);
    void ensure_text_glyph_dynamic_buffer(VkDeviceSize bytes, std::size_t index);
    void ensure_text_tile_buffer(VkDeviceSize bytes, std::size_t index,
                                 bool indices);
    void begin_command_buffer();
    void wait_for_standalone();
    void wait_for_pending();
    std::uint64_t submit(bool wait_for_completion = true);
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
