#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include "vulkan_ring_depths.hpp"
#include "vulkan_surface_materializer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d::backends::vulkan {

// Exact frame-batch slot ownership (TICKET-DEFERRED-SURFACE-SLOT-HINT). Bit i
// of a mask is frame-batch slot i. A handle's mask is the set of slots whose
// recorded commands resolved it since its last release.
inline constexpr std::uint8_t frame_slot_bit(std::size_t slot) noexcept {
    return static_cast<std::uint8_t>(1u << slot);
}

// True when no slot named in `mask` is still busy. `busy` is indexed by
// frame-batch slot; slots beyond its extent count as idle. A slot is busy while
// its submission is in flight or while it is being recorded.
inline bool recording_slots_retired(std::uint8_t mask,
                                    std::span<const bool> busy) noexcept {
    for (std::size_t slot = 0; slot < 8; ++slot) {
        if ((mask & frame_slot_bit(slot)) != 0 && slot < busy.size() &&
            busy[slot]) {
            return false;
        }
    }
    return true;
}

// Sole authority for Vulkan-native surface binding/materialization state and
// physical-slot selection. Planned bindings are exact: the authority never
// silently substitutes another slot. Dynamic slot selection is confined to
// the explicitly unplanned/lazy path.
template <typename ImageT, typename OwnerT>
class VulkanSurfaceAuthority {
public:
    // The surface store is an independent contention domain.  Callers that
    // touch the store through VulkanBackend take this lock; command recording
    // additionally takes VulkanSubmissionAuthority::mutex.  Keeping the lock
    // beside the state makes the authority boundary explicit and prevents a
    // backend-wide mutex from becoming the accidental owner of surface
    // concurrency.
    mutable std::mutex mutex;

    // Frame-transient physical images are expensive to materialize (especially
    // at 1080p). Keep a bounded warm pool after their logical binding is
    // retired; the next compiled frame can rebind the exact slot without
    // recreating VkImage/device memory. The bound and its relation to the
    // submission ring are owned by vulkan_ring_depths.hpp, together with the
    // peak-VRAM warning that governs the daemon's job serialization.
    static constexpr std::size_t kMaxRetainedTransientSlots =
        kMaxRetainedTransientSurfaceSlots;

    struct PhysicalSurface {
        ImageT image{};
        runtime::SurfaceDesc desc{};
    };

    // ── Recording lease + generation token (audit N4) ────────────────────
    //
    // A recording op resolves its surfaces under this store's lock, then
    // releases it and drives the submission ring. `SurfaceLease` is that
    // hand-off: it carries the resolved image, its physical slot and the
    // slot's generation token. The writer paths below refuse to erase or
    // re-materialize a leased slot, so the pin is what makes the hand-off
    // safe; the token turns a pin-enforcement bug into a loud failure instead
    // of a silent use-after-free.
    //
    // `slot_tokens` is a fixed array so a lease can read its slot's token
    // without the store lock (stable addresses + atomic reads). Slots beyond
    // the array are untracked and rely on the pin alone.
    static constexpr std::size_t kTrackedSurfaceSlots = 4096;
    static constexpr std::size_t kInvalidSurfaceSlot =
        std::numeric_limits<std::size_t>::max();

    struct SurfaceLease {
        ImageT* image{nullptr};
        std::size_t slot{kInvalidSurfaceSlot};
        std::uint64_t token{0};
        runtime::RenderSurfaceHandle handle{runtime::kInvalidRenderSurfaceHandle};
        bool valid{false};
    };

    std::array<std::atomic<std::uint64_t>, kTrackedSurfaceSlots> slot_tokens{};
    // Slot pin counts. Store-mutex guarded: only writers (prune/materialize)
    // and lease acquisition/release read them while holding that lock.
    std::unordered_map<std::size_t, std::size_t> slot_pins{};

    std::unordered_map<std::size_t, PhysicalSurface> physical_surfaces{};
    std::unordered_map<runtime::RenderSurfaceHandle, std::size_t> surface_bindings{};
    // Explicit planned aliasing. The compiled planner deliberately backs
    // lifetime-disjoint logical handles with one physical slot (that is why
    // gpu_surface_copy_frames can reach 0). The plan is the only authority
    // that may declare such a pair, so it registers every compiled occupant
    // of a slot here before binding. A slot may then be shared only by handles
    // the same plan named; any other occupant — notably the NVENC/encode ring
    // created outside the plan — remains a hard failure (the 077a44275 guard).
    std::unordered_map<std::size_t,
                       std::unordered_set<runtime::RenderSurfaceHandle>>
        planned_alias_groups{};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    // CUDA releases stay pending until the surface is actually referenced by
    // Vulkan. Only acquired surfaces participate in the current submission.
    std::unordered_set<std::size_t> cuda_pending_surfaces{};
    std::unordered_set<std::size_t> cuda_ready_surfaces{};

    bool acquire_pending_cuda_surface(std::size_t slot) {
        if (!cuda_pending_surfaces.contains(slot)) return false;
        cuda_ready_surfaces.insert(slot);
        cuda_pending_surfaces.erase(slot);
        return true;
    }
    std::unordered_set<std::size_t> cuda_export_ready_surfaces{};
#endif
    struct DeferredSurfaceRelease {
        runtime::RenderSurfaceHandle handle{runtime::kInvalidRenderSurfaceHandle};
        // Most recent owning slot (highest set bit of slot_mask); diagnostic.
        std::size_t slot{0};
        std::uint64_t requested_frame_index{0};
        // Exact set of frame-batch slots that referenced `handle` since its last
        // release. The release waits until every slot named here is retired.
        std::uint8_t slot_mask{0};
    };
    std::vector<DeferredSurfaceRelease> deferred_surface_releases{};
    // Per-handle recording-slot masks (see frame_slot_bit). Written only on the
    // recording path, which holds the submission domain, and read by the
    // release/retire paths, which hold both domains.
    std::unordered_map<runtime::RenderSurfaceHandle, std::uint8_t>
        recording_slot_masks{};
    // Surface-lifecycle peaks are guarded by `mutex`, like the containers
    // they describe. `retained_bindings_high_water` counts live logical
    // handle→slot bindings (including planned aliases); the pending peak counts
    // queued deferred releases, not cumulative releases.
    std::uint64_t pending_release_high_water{0};
    std::uint64_t retained_bindings_high_water{0};
    std::uint64_t release_delay_frames{0};
    std::uint64_t release_delay_frames_high_water{0};
    std::uint64_t overflow_wait_count{0};
    std::unordered_set<runtime::RenderSurfaceHandle> unplanned_surface_handles{};
    static constexpr std::size_t kFrameBatchSlotCount = 6;
    std::array<std::vector<runtime::RenderSurfaceHandle>, kFrameBatchSlotCount>
        frame_slot_transient_handles{};
    // P0/P1 hardening: compiled placement slots (bind()) are plan-local 0..N
    // but were aliased into the same size_t namespace as the backend-global
    // unplanned allocator (ensure()/next_slot).  Start the unplanned allocator
    // high so a plan slot 0 never collides with a backend-global slot 0 under
    // concurrent jobs with divergent plans.  Compiled vs external is now a
    // different numeric range; logical equality is by slot+domain, not slot
    // alone.  See docs/architecture/VULKAN-AUTHORITY-MAP.md.
    static constexpr std::size_t kUnplannedSlotBase = 1u << 20;
    std::size_t next_slot{kUnplannedSlotBase};

    // Policy owns binding state; materializer owns only the physical Vulkan
    // mechanism. It receives an already-selected Image record and never a
    // physical slot, so it cannot make placement decisions.
    VulkanSurfaceMaterializer<ImageT, OwnerT> materializer_{};
    OwnerT* owner_{nullptr};

    // ── Planned alias policy ─────────────────────────────────────────────

    /// Register ``handle`` as a compiled occupant of ``slot``. Called for
    /// every allocation of the plan before the exact binds so the slot's
    /// group is exactly the plan's occupant set.
    void register_planned_occupant(
        std::size_t slot, runtime::RenderSurfaceHandle handle) {
        planned_alias_groups[slot].insert(handle);
    }

    /// Convenience for callers/tests that know one alias pair explicitly.
    void register_planned_alias(runtime::RenderSurfaceHandle handle_a,
                                runtime::RenderSurfaceHandle handle_b,
                                std::size_t slot) {
        if (handle_a == runtime::kInvalidRenderSurfaceHandle ||
            handle_b == runtime::kInvalidRenderSurfaceHandle ||
            handle_a == handle_b) {
            return;
        }
        auto& group = planned_alias_groups[slot];
        group.insert(handle_a);
        group.insert(handle_b);
    }

    /// The plan's alias declarations are per-plan, so the compiled batch
    /// rebuilds them from scratch at every bind.
    void clear_planned_aliases() noexcept { planned_alias_groups.clear(); }

    [[nodiscard]] bool is_registered_alias(
        runtime::RenderSurfaceHandle handle,
        runtime::RenderSurfaceHandle other,
        std::size_t slot) const noexcept {
        const auto it = planned_alias_groups.find(slot);
        return it != planned_alias_groups.end() &&
               it->second.contains(handle) && it->second.contains(other);
    }

    [[nodiscard]] bool valid(runtime::RenderSurfaceHandle handle) const noexcept {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return false;
        const auto surface = physical_surfaces.find(binding->second);
        return surface != physical_surfaces.end() &&
               surface->second.image.image != VK_NULL_HANDLE;
    }

    [[nodiscard]] std::size_t physical_count() const noexcept {
        return physical_surfaces.size();
    }

    [[nodiscard]] std::size_t binding_count() const noexcept {
        return surface_bindings.size();
    }

    [[nodiscard]] std::size_t deferred_release_count() const noexcept {
        return deferred_surface_releases.size();
    }

    /// Record that the frame-batch slot ``slot`` referenced ``handle``.
    void note_recording_slot(runtime::RenderSurfaceHandle handle,
                             std::size_t slot) {
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            slot >= kFrameBatchSlotCount) {
            return;
        }
        recording_slot_masks[handle] |= frame_slot_bit(slot);
    }

    [[nodiscard]] std::uint8_t recording_slot_mask(
        runtime::RenderSurfaceHandle handle) const noexcept {
        const auto it = recording_slot_masks.find(handle);
        return it == recording_slot_masks.end() ? std::uint8_t{0} : it->second;
    }

    [[nodiscard]] bool can_retire_surface(
        runtime::RenderSurfaceHandle handle, std::span<const bool> busy) const noexcept {
        return recording_slots_retired(recording_slot_mask(handle), busy);
    }

    void forget_recording_slots(runtime::RenderSurfaceHandle handle) {
        recording_slot_masks.erase(handle);
    }

    void enqueue_deferred_release(DeferredSurfaceRelease release) {
        deferred_surface_releases.push_back(release);
        pending_release_high_water = std::max(
            pending_release_high_water,
            static_cast<std::uint64_t>(deferred_surface_releases.size()));
    }

    void note_binding_created() noexcept {
        retained_bindings_high_water = std::max(
            retained_bindings_high_water,
            static_cast<std::uint64_t>(surface_bindings.size()));
    }

    void note_release_delay(std::uint64_t requested_frame_index,
                            std::uint64_t released_frame_index) noexcept {
        if (released_frame_index < requested_frame_index) return;
        const auto delay = released_frame_index - requested_frame_index;
        release_delay_frames = delay >
                std::numeric_limits<std::uint64_t>::max() - release_delay_frames
            ? std::numeric_limits<std::uint64_t>::max()
            : release_delay_frames + delay;
        release_delay_frames_high_water = std::max(
            release_delay_frames_high_water, delay);
    }

    [[nodiscard]] bool note_overflow_if_needed(std::size_t capacity) noexcept {
        if (deferred_surface_releases.size() <= capacity) return false;
        ++overflow_wait_count;
        return true;
    }

    // ── Generation token (lock-free reads) ───────────────────────────────

    [[nodiscard]] bool slot_token_tracked(std::size_t slot) const noexcept {
        return slot < kTrackedSurfaceSlots;
    }

    [[nodiscard]] std::uint64_t slot_token(std::size_t slot) const noexcept {
        return slot_token_tracked(slot)
            ? slot_tokens[slot].load(std::memory_order_acquire)
            : 0;
    }

    void bump_slot_token(std::size_t slot) noexcept {
        if (slot_token_tracked(slot)) {
            slot_tokens[slot].fetch_add(1, std::memory_order_acq_rel);
        }
    }

    // ── Lease lifecycle (store lock held) ────────────────────────────────

    /// Pin the handle's physical slot and hand back the resolved image.
    /// Throws when the handle is not bound — a recording op must never
    /// silently record against a surface it never resolved.
    [[nodiscard]] SurfaceLease acquire_lease(
        runtime::RenderSurfaceHandle handle) {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) {
            throw std::invalid_argument(
                "Vulkan recording lease references an unbound surface " +
                std::to_string(handle));
        }
        const auto physical = physical_surfaces.find(binding->second);
        if (physical == physical_surfaces.end()) {
            throw std::invalid_argument(
                "Vulkan recording lease has no backing physical image");
        }
        ++slot_pins[binding->second];
        return SurfaceLease{&physical->second.image, binding->second,
                            slot_token(binding->second), handle, true};
    }

    void release_lease(const SurfaceLease& lease) noexcept {
        if (!lease.valid) return;
        const auto it = slot_pins.find(lease.slot);
        if (it == slot_pins.end()) return;
        if (it->second > 1) {
            --it->second;
        } else {
            slot_pins.erase(it);
        }
    }

    [[nodiscard]] bool slot_pinned(std::size_t slot) const noexcept {
        const auto it = slot_pins.find(slot);
        return it != slot_pins.end() && it->second != 0;
    }

    /// Lock-free. A recording op calls this between the state lookup and the
    /// ring registration; false means the slot changed or was erased, so the
    /// lease must be re-acquired (or the op must fail loudly).
    [[nodiscard]] bool revalidate(const SurfaceLease& lease) const noexcept {
        if (!lease.valid || lease.image == nullptr) return false;
        // Untracked slots (beyond the token array) rely on the pin alone.
        if (!slot_token_tracked(lease.slot)) return true;
        return slot_token(lease.slot) == lease.token;
    }

    void clear_leases() noexcept { slot_pins.clear(); }

    /// Writer-side half of the pin invariant: a slot under an in-flight
    /// recording lease must not be re-materialized or erased. Failing loudly
    /// here is strictly better than destroying a VkImage another thread is
    /// still recording into.
    void require_not_leased(std::size_t slot, const char* mutation) const {
        if (!slot_pinned(slot)) return;
        throw std::logic_error(
            std::string{"Vulkan surface slot "} + std::to_string(slot) +
            " cannot " + mutation +
            " while a recording lease holds it (audit N4 pin invariant)");
    }

    [[nodiscard]] bool is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return false;
        const auto physical = physical_surfaces.find(binding->second);
        return physical != physical_surfaces.end() &&
               physical->second.desc.lifetime == runtime::LifetimeClass::JobPersistent;
    }

    [[nodiscard]] bool slot_in_use(std::size_t slot) const noexcept {
        for (const auto& [handle, bound_slot] : surface_bindings) {
            (void)handle;
            if (bound_slot == slot) return true;
        }
        return false;
    }

    [[nodiscard]] std::size_t bound_slot(runtime::RenderSurfaceHandle handle) const {
        const auto it = surface_bindings.find(handle);
        if (it == surface_bindings.end()) {
            throw std::invalid_argument(
                "Vulkan surface handle " + std::to_string(handle) +
                " is not bound to a physical slot (bindings=" +
                std::to_string(surface_bindings.size()) +
                ", physical_surfaces=" +
                std::to_string(physical_surfaces.size()) + ")");
        }
        return it->second;
    }

    ImageT& resolve(runtime::RenderSurfaceHandle handle) {
        const auto slot = bound_slot(handle);
        const auto physical = physical_surfaces.find(slot);
        if (physical == physical_surfaces.end()) {
            throw std::invalid_argument("Vulkan physical slot has no backing image");
        }
        return physical->second.image;
    }

    [[nodiscard]] bool slot_has_initialized_occupant(
        std::size_t slot, runtime::RenderSurfaceHandle self) const noexcept {
        const auto physical = physical_surfaces.find(slot);
        if (physical == physical_surfaces.end() ||
            !physical->second.image.initialized) {
            return false;
        }
        for (const auto& [handle, bound_slot] : surface_bindings) {
            if (handle != self && bound_slot == slot) return true;
        }
        return false;
    }

    // Compiled/materialized path. The caller already has an exact physical
    // slot from CommandPlan/CompiledResourceTable. Placement is immutable here:
    // no pinning, diversion, reuse search or next_slot mutation is permitted.
    ImageT& bind(runtime::RenderSurfaceHandle handle,
                 std::size_t slot,
                 const runtime::SurfaceDesc& desc,
                 std::span<const runtime::RenderSurfaceHandle> planned_occupants = {}) {
        require_owner();
        return bind_planned_exact(handle, slot, desc, planned_occupants);
    }

    // Explicit compatibility/unplanned path. This is the only API allowed to
    // select or reselect a physical slot dynamically. Demolition Debt exit
    // condition: compiled execution never calls ensure(); once all direct
    // callers are planned, this policy can be removed entirely.
    ImageT& ensure(runtime::RenderSurfaceHandle handle,
                   const runtime::SurfaceDesc& desc) {
        require_owner();
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            (desc.format != runtime::PixelFormat::Rgba32Float &&
             desc.format != runtime::PixelFormat::Rgba8Unorm &&
             desc.format != runtime::PixelFormat::R8Unorm &&
             desc.format != runtime::PixelFormat::Nv12 &&
             desc.format != runtime::PixelFormat::P010) ||
            desc.width == 0 || desc.height == 0) {
            throw std::invalid_argument(
                "Vulkan surface requires a valid non-empty description");
        }

        const auto binding = surface_bindings.find(handle);
        if (binding != surface_bindings.end()) {
            auto& physical = physical_surfaces.at(binding->second);
            const bool exportable = desc.residency.exportable ||
                                    desc.residency.encoder_compatible;
            if (physical.image.image == VK_NULL_HANDLE ||
                physical.image.width != desc.width ||
                physical.image.height != desc.height ||
                physical.desc.format != desc.format ||
                physical.image.exportable != exportable) {
                require_not_leased(binding->second, "re-materialize");
                forget_cuda_slot(binding->second);
                materializer_.materialize(physical.image, desc, exportable);
                bump_slot_token(binding->second);
                physical.desc = desc;
            }
            physical.image.text_atlas_encoding = desc.text_atlas_encoding;
            owner_->ensure_descriptor_set();
            return physical.image;
        }

        // Persistent assets never alias transient slots. For transients, prefer
        // an exact compatible unused slot, then any unused transient slot.
        if (desc.lifetime != runtime::LifetimeClass::JobPersistent) {
            for (auto& [slot, physical] : physical_surfaces) {
                if (!slot_in_use(slot) &&
                    owner_->surface_compatible(physical.desc, desc)) {
                    owner_->ensure_descriptor_set();
                    return bind_unplanned(handle, slot, desc);
                }
            }
            for (auto& [slot, physical] : physical_surfaces) {
                if (!slot_in_use(slot) &&
                    physical.desc.lifetime != runtime::LifetimeClass::JobPersistent) {
                    owner_->ensure_descriptor_set();
                    return bind_unplanned(handle, slot, desc);
                }
            }
        }

        owner_->ensure_descriptor_set();
        return bind_unplanned(handle, next_slot++, desc);
    }

    void prune_unused_slots() {
        prune_unused_slots_impl(false, kMaxRetainedTransientSlots);
    }

    void prune_unused_slots(std::size_t max_retained) {
        prune_unused_slots_impl(false, max_retained);
    }

    // A compiled plan can leave behind transient slots that were previously
    // bound by create_surface() but are no longer occupied after the backend
    // derives its content-aware placement. Reclaim those slots immediately;
    // the normal prune policy intentionally retains a warm pool.
    void reclaim_unused_transient_slots() {
        prune_unused_slots_impl(true, 0);
    }

private:
    void prune_unused_slots_impl(bool force, std::size_t max_retained) {
        require_owner();
        std::size_t unused_transient = 0;
        for (const auto& [slot, physical] : physical_surfaces) {
            (void)slot;
            if (physical.desc.lifetime != runtime::LifetimeClass::JobPersistent &&
                !slot_in_use(slot) && !slot_pinned(slot)) {
                ++unused_transient;
            }
        }
        if (!force && unused_transient <= max_retained) return;

        for (auto it = physical_surfaces.begin(); it != physical_surfaces.end();) {
            // A leased slot is skipped: destroying its image would pull the
            // VkImage out from under an in-flight recording.
            if (slot_in_use(it->first) || slot_pinned(it->first) ||
                it->second.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                ++it;
                continue;
            }
            materializer_.destroy(it->second.image);
            forget_cuda_slot(it->first);
            bump_slot_token(it->first);
            it = physical_surfaces.erase(it);
            --unused_transient;
            if (unused_transient == 0 ||
                (!force && unused_transient <= max_retained)) {
                break;
            }
        }

        std::size_t max_active_slot = 0;
        for (const auto& [slot, physical] : physical_surfaces) {
            max_active_slot = std::max(max_active_slot, slot);
        }
        if (!physical_surfaces.empty()) {
            next_slot = std::max(max_active_slot + 1, kUnplannedSlotBase);
        } else {
            next_slot = kUnplannedSlotBase;
        }
    }

public:
    // ── Per-frame-batch-slot transient retirement ────────────────────────
    //
    // Frame-transient images that were materialized *while a frame batch was
    // recording* are owned by that batch slot: once the slot's fence signals,
    // the GPU no longer reads them and their logical binding can be dropped so
    // the next occupant re-materializes content instead of trusting stale
    // bytes.  The record is deliberately scoped: only handles materialized
    // inside an active frame slot are recorded, so a host-provided surface
    // created by the transfer ingress outside a batch (for example
    // create_surface() during preparation) is never retired by a frame
    // boundary.  That distinction is exactly what made the earlier "retire
    // every transient in the slot namespace" remedy erase host content and
    // get reverted (TICKET-AUDIT-2026-09-12-REMEDIATION §7).

    /// Record a transient surface materialized while ``slot`` was the active
    /// frame-batch slot.  Idempotent: a handle appears at most once per slot.
    void record_frame_slot_transient(std::size_t slot,
                                     runtime::RenderSurfaceHandle handle) {
        if (slot >= kFrameBatchSlotCount ||
            handle == runtime::kInvalidRenderSurfaceHandle) {
            return;
        }
        auto& list = frame_slot_transient_handles[slot];
        if (std::find(list.begin(), list.end(), handle) == list.end()) {
            list.push_back(handle);
        }
    }

    /// Retire the handles recorded for a completed frame-batch slot: drop each
    /// logical binding and clear the retained image's ``initialized`` flag so
    /// the next occupant re-materializes content.  The physical image stays in
    /// the warm pool (bounded by ``prune_unused_slots``).  Returns the number
    /// of bindings released so the caller can account for them.  Handles that
    /// were never recorded for this slot — host surfaces, another slot's
    /// transients, persistent assets — are never touched.
    std::size_t retire_frame_slot_transients(
        std::size_t slot, std::span<const bool> busy = {}) {
        if (slot >= kFrameBatchSlotCount) return 0;
        auto& list = frame_slot_transient_handles[slot];
        const bool had_records = !list.empty();
        std::size_t released = 0;
        for (const auto handle : list) {
            const auto binding = surface_bindings.find(handle);
            if (binding == surface_bindings.end()) continue;
            // Exact ownership: this slot's reference is gone, but a handle can
            // also be recorded into a slot that is still busy. Keep the binding
            // then; that slot's own retirement releases it.
            const auto mask_it = recording_slot_masks.find(handle);
            if (mask_it != recording_slot_masks.end()) {
                mask_it->second = static_cast<std::uint8_t>(
                    mask_it->second & ~frame_slot_bit(slot));
                if (!recording_slots_retired(mask_it->second, busy)) continue;
            }
            const auto phys_slot = binding->second;
            surface_bindings.erase(binding);
            unplanned_surface_handles.erase(handle);
            forget_recording_slots(handle);
            const auto it = physical_surfaces.find(phys_slot);
            if (it != physical_surfaces.end() && !slot_in_use(phys_slot)) {
                it->second.image.initialized = false;
            }
            ++released;
        }
        list.clear();
        if (had_records) {
            prune_unused_slots(kMaxRetainedTransientSlots);
        }
        return released;
    }

protected:
    void require_owner() {
        if (owner_ == nullptr) {
            throw std::logic_error("Vulkan surface authority has no owner");
        }
        materializer_.set_owner(owner_);
    }

    // CUDA ownership markers belong to a physical image generation. When a
    // slot is destroyed or rematerialized, discard every pending handoff for
    // that generation before the slot can hold a generic (non-exportable)
    // graph surface. Otherwise a later Vulkan recording may try to acquire a
    // stale CUDA token from the new image.
    void forget_cuda_slot(std::size_t slot) noexcept {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        cuda_pending_surfaces.erase(slot);
        cuda_ready_surfaces.erase(slot);
        cuda_export_ready_surfaces.erase(slot);
#else
        (void)slot;
#endif
    }

    ImageT& bind_planned_exact(runtime::RenderSurfaceHandle handle,
                               std::size_t slot,
                               const runtime::SurfaceDesc& desc,
                               std::span<const runtime::RenderSurfaceHandle>
                                   planned_occupants) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_surface = physical_surfaces.find(previous->second);
            if (old_surface != physical_surfaces.end() &&
                old_surface->second.image.initialized &&
                old_surface->second.desc.lifetime !=
                    runtime::LifetimeClass::FrameTransient) {
                // The plan may re-slot a transient binding after its content
                // has retired. Persistent or initialized external content is
                // never silently replaced.
                throw std::logic_error(
                    "Vulkan planned persistent surface attempted to change physical slot");
            }
            surface_bindings.erase(previous);
        }

        // Planned graph resources must not alias a persistent video slot even
        // before that slot has received its first GPU write.  The old check
        // considered only initialized occupants, allowing the first plan
        // materialization to steal the NVENC ring's physical image.
        for (const auto& [bound_handle, bound_slot] : surface_bindings) {
            const bool planned_alias = std::find(
                planned_occupants.begin(), planned_occupants.end(), bound_handle) !=
                planned_occupants.end();
            if (bound_handle != handle && bound_slot == slot && !planned_alias &&
                !is_registered_alias(handle, bound_handle, slot)) {
                throw std::logic_error(
                    "Vulkan planned surface physical slot is already reserved");
            }
        }
        return materialize_binding(handle, slot, desc);
    }

    ImageT& bind_unplanned(runtime::RenderSurfaceHandle handle,
                           std::size_t slot,
                           const runtime::SurfaceDesc& desc) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_slot = previous->second;
            const auto old_it = physical_surfaces.find(old_slot);
            const bool pinned = old_it != physical_surfaces.end() &&
                                old_it->second.image.initialized;
            if (pinned) {
                slot = old_slot;
            } else {
                surface_bindings.erase(previous);
            }
        }
        // A logical binding reserves its physical slot even before the image
        // has received its first upload.  Checking only initialized occupants
        // lets a transient graph surface alias a persistent NVENC/source
        // surface during the first frame; retiring the transient then clears
        // the shared image and invalidates the encode ring on frame four.
        while (slot_in_use(slot) ||
               (physical_surfaces.contains(slot) &&
                physical_surfaces.at(slot).desc.lifetime ==
                    runtime::LifetimeClass::JobPersistent)) {
            slot = next_slot++;
        }
        return materialize_binding(handle, slot, desc);
    }

    ImageT& materialize_binding(runtime::RenderSurfaceHandle handle,
                                std::size_t slot,
                                const runtime::SurfaceDesc& desc) {
        require_not_leased(slot, "be re-materialized");
        auto& physical = physical_surfaces[slot];
        owner_->stats.physical_surfaces_peak = std::max(
            owner_->stats.physical_surfaces_peak,
            static_cast<std::uint64_t>(physical_surfaces.size()));

        // DEMOLISHED (P1.4): the plan_preallocated fast-path was dead —
        // plan_preallocated was always false.  Lazy materialization below is
        // the only path.

        const bool exportable = desc.residency.exportable ||
                                desc.residency.encoder_compatible;
        if (physical.image.image == VK_NULL_HANDLE ||
            physical.image.width != desc.width ||
            physical.image.height != desc.height ||
            physical.desc.format != desc.format ||
            physical.image.exportable != exportable) {
            forget_cuda_slot(slot);
            materializer_.materialize(physical.image, desc, exportable);
        }
        physical.desc = desc;
        physical.image.text_atlas_encoding = desc.text_atlas_encoding;
        surface_bindings[handle] = slot;
        note_binding_created();
        bump_slot_token(slot);
        return physical.image;
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
