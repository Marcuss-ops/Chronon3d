#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include "vulkan_surface_materializer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d::backends::vulkan {

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
    // recreating VkImage/device memory. Persistent CUDA/NVENC surfaces are
    // never counted or evicted by this policy.
    static constexpr std::size_t kMaxRetainedTransientSlots = 64;

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
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    std::unordered_set<std::size_t> cuda_ready_surfaces{};
    std::unordered_set<std::size_t> cuda_export_ready_surfaces{};
#endif
    std::vector<runtime::RenderSurfaceHandle> deferred_surface_releases{};
    std::unordered_set<runtime::RenderSurfaceHandle> unplanned_surface_handles{};
    std::size_t next_slot{0};

    // Policy owns binding state; materializer owns only the physical Vulkan
    // mechanism. It receives an already-selected Image record and never a
    // physical slot, so it cannot make placement decisions.
    VulkanSurfaceMaterializer<ImageT, OwnerT> materializer_{};
    OwnerT* owner_{nullptr};

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
                 const runtime::SurfaceDesc& desc) {
        require_owner();
        return bind_planned_exact(handle, slot, desc);
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
            if (physical.image.image == VK_NULL_HANDLE ||
                physical.image.width != desc.width ||
                physical.image.height != desc.height ||
                physical.desc.format != desc.format) {
                require_not_leased(binding->second, "re-materialize");
                materializer_.materialize(physical.image, desc);
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
        require_owner();
        std::size_t unused_transient = 0;
        for (const auto& [slot, physical] : physical_surfaces) {
            (void)slot;
            if (physical.desc.lifetime != runtime::LifetimeClass::JobPersistent &&
                !slot_in_use(slot) && !slot_pinned(slot)) {
                ++unused_transient;
            }
        }
        if (unused_transient <= kMaxRetainedTransientSlots) return;

        for (auto it = physical_surfaces.begin(); it != physical_surfaces.end();) {
            // A leased slot is skipped: destroying its image would pull the
            // VkImage out from under an in-flight recording.
            if (slot_in_use(it->first) || slot_pinned(it->first) ||
                it->second.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                ++it;
                continue;
            }
            materializer_.destroy(it->second.image);
            bump_slot_token(it->first);
            it = physical_surfaces.erase(it);
            if (--unused_transient <= kMaxRetainedTransientSlots) break;
        }
    }

protected:
    void require_owner() {
        if (owner_ == nullptr) {
            throw std::logic_error("Vulkan surface authority has no owner");
        }
        materializer_.set_owner(owner_);
    }

    ImageT& bind_planned_exact(runtime::RenderSurfaceHandle handle,
                               std::size_t slot,
                               const runtime::SurfaceDesc& desc) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_surface = physical_surfaces.find(previous->second);
            if (old_surface != physical_surfaces.end() &&
                old_surface->second.image.initialized) {
                throw std::logic_error(
                    "Vulkan planned surface attempted to change physical slot");
            }
            surface_bindings.erase(previous);
        }

        // Planned graph resources must not alias a persistent video slot even
        // before that slot has received its first GPU write.  The old check
        // considered only initialized occupants, allowing the first plan
        // materialization to steal the NVENC ring's physical image.
        for (const auto& [bound_handle, bound_slot] : surface_bindings) {
            if (bound_handle != handle && bound_slot == slot) {
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

        if (physical.image.image == VK_NULL_HANDLE ||
            physical.image.width != desc.width ||
            physical.image.height != desc.height ||
            physical.desc.format != desc.format) {
            materializer_.materialize(physical.image, desc);
        }
        physical.desc = desc;
        physical.image.text_atlas_encoding = desc.text_atlas_encoding;
        surface_bindings[handle] = slot;
        bump_slot_token(slot);
        return physical.image;
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
