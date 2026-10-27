#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
template <typename ImageT, typename OwnerT>
bool VulkanSurfaceAuthority<ImageT, OwnerT>::acquire_pending_cuda_surface(std::size_t slot) {
        if (!cuda_pending_surfaces.contains(slot)) return false;
        cuda_ready_surfaces.insert(slot);
        cuda_pending_surfaces.erase(slot);
        return true;
    }
#endif

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::register_planned_occupant(
        std::size_t slot, runtime::RenderSurfaceHandle handle) {
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            return;
        }
        auto& occupants = planned_alias_groups[slot].occupants;
        if (std::find(occupants.begin(), occupants.end(), handle) ==
            occupants.end()) {
            occupants.push_back(handle);
        }
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::register_planned_alias(runtime::RenderSurfaceHandle handle_a,
                                runtime::RenderSurfaceHandle handle_b,
                                std::size_t slot) {
        if (handle_a == runtime::kInvalidRenderSurfaceHandle ||
            handle_b == runtime::kInvalidRenderSurfaceHandle ||
            handle_a == handle_b) {
            return;
        }
        register_planned_occupant(slot, handle_a);
        register_planned_occupant(slot, handle_b);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::clear_planned_aliases() noexcept {
        for (auto& entry : planned_alias_groups) {
            entry.second.occupants.clear();
        }
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::is_registered_alias(
        runtime::RenderSurfaceHandle handle,
        runtime::RenderSurfaceHandle other,
        std::size_t slot) const noexcept {
        // An alias is two DISTINCT valid handles of one plan group: a self-pair
        // or an invalid handle never authorizes anything.
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            other == runtime::kInvalidRenderSurfaceHandle ||
            handle == other) {
            return false;
        }
        const auto it = planned_alias_groups.find(slot);
        if (it == planned_alias_groups.end()) return false;
        const auto& occupants = it->second.occupants;
        return std::find(occupants.begin(), occupants.end(), handle) !=
                   occupants.end() &&
               std::find(occupants.begin(), occupants.end(), other) !=
                   occupants.end();
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::valid(runtime::RenderSurfaceHandle handle) const noexcept {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return false;
        const auto surface = physical_surfaces.find(binding->second);
        return surface != physical_surfaces.end() &&
               surface->second.image.image != VK_NULL_HANDLE;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] std::size_t VulkanSurfaceAuthority<ImageT, OwnerT>::physical_count() const noexcept {
        return physical_surfaces.size();
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] std::size_t VulkanSurfaceAuthority<ImageT, OwnerT>::binding_count() const noexcept {
        return surface_bindings.size();
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] std::size_t VulkanSurfaceAuthority<ImageT, OwnerT>::deferred_release_count() const noexcept {
        return deferred_surface_releases.size();
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::note_recording_slot(runtime::RenderSurfaceHandle handle,
                             std::size_t slot) {
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            slot >= kFrameBatchSlotCount) {
            return;
        }
        recording_slot_masks[handle] |= frame_slot_bit(slot);
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] std::uint8_t VulkanSurfaceAuthority<ImageT, OwnerT>::recording_slot_mask(
        runtime::RenderSurfaceHandle handle) const noexcept {
        const auto it = recording_slot_masks.find(handle);
        return it == recording_slot_masks.end() ? std::uint8_t{0} : it->second;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::can_retire_surface(
        runtime::RenderSurfaceHandle handle, std::span<const bool> busy) const noexcept {
        return recording_slots_retired(recording_slot_mask(handle), busy);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::forget_recording_slots(runtime::RenderSurfaceHandle handle) {
        recording_slot_masks.erase(handle);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::enqueue_deferred_release(DeferredSurfaceRelease release) {
        deferred_surface_releases.push_back(release);
        pending_release_high_water = std::max(
            pending_release_high_water,
            static_cast<std::uint64_t>(deferred_surface_releases.size()));
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::note_binding_created() noexcept {
        retained_bindings_high_water = std::max(
            retained_bindings_high_water,
            static_cast<std::uint64_t>(surface_bindings.size()));
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::note_release_delay(std::uint64_t requested_frame_index,
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

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::note_overflow_if_needed(std::size_t capacity) noexcept {
        if (deferred_surface_releases.size() <= capacity) return false;
        ++overflow_wait_count;
        return true;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::slot_token_tracked(std::size_t slot) const noexcept {
        return slot < kTrackedSurfaceSlots;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] std::uint64_t VulkanSurfaceAuthority<ImageT, OwnerT>::slot_token(std::size_t slot) const noexcept {
        return slot_token_tracked(slot)
            ? slot_tokens[slot].load(std::memory_order_acquire)
            : 0;
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::bump_slot_token(std::size_t slot) noexcept {
        if (slot_token_tracked(slot)) {
            slot_tokens[slot].fetch_add(1, std::memory_order_acq_rel);
        }
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] typename VulkanSurfaceAuthority<ImageT, OwnerT>::SurfaceLease VulkanSurfaceAuthority<ImageT, OwnerT>::acquire_lease(
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

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::release_lease(const SurfaceLease& lease) noexcept {
        if (!lease.valid) return;
        const auto it = slot_pins.find(lease.slot);
        if (it == slot_pins.end()) return;
        if (it->second > 1) {
            --it->second;
        } else {
            slot_pins.erase(it);
        }
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::slot_pinned(std::size_t slot) const noexcept {
        const auto it = slot_pins.find(slot);
        return it != slot_pins.end() && it->second != 0;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::revalidate(const SurfaceLease& lease) const noexcept {
        if (!lease.valid || lease.image == nullptr) return false;
        // Untracked slots (beyond the token array) rely on the pin alone.
        if (!slot_token_tracked(lease.slot)) return true;
        return slot_token(lease.slot) == lease.token;
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::clear_leases() noexcept { slot_pins.clear(); }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::require_not_leased(std::size_t slot, const char* mutation) const {
        if (!slot_pinned(slot)) return;
        throw std::logic_error(
            std::string{"Vulkan surface slot "} + std::to_string(slot) +
            " cannot " + mutation +
            " while a recording lease holds it (audit N4 pin invariant)");
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return false;
        const auto physical = physical_surfaces.find(binding->second);
        return physical != physical_surfaces.end() &&
               physical->second.desc.lifetime == runtime::LifetimeClass::JobPersistent;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::slot_in_use(std::size_t slot) const noexcept {
        for (const auto& [handle, bound_slot] : surface_bindings) {
            (void)handle;
            if (bound_slot == slot) return true;
        }
        return false;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] std::size_t VulkanSurfaceAuthority<ImageT, OwnerT>::bound_slot(runtime::RenderSurfaceHandle handle) const {
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

template <typename ImageT, typename OwnerT>
ImageT& VulkanSurfaceAuthority<ImageT, OwnerT>::resolve(runtime::RenderSurfaceHandle handle) {
        const auto slot = bound_slot(handle);
        const auto physical = physical_surfaces.find(slot);
        if (physical == physical_surfaces.end()) {
            throw std::invalid_argument("Vulkan physical slot has no backing image");
        }
        return physical->second.image;
    }

template <typename ImageT, typename OwnerT>
[[nodiscard]] bool VulkanSurfaceAuthority<ImageT, OwnerT>::slot_has_initialized_occupant(
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

template <typename ImageT, typename OwnerT>
ImageT& VulkanSurfaceAuthority<ImageT, OwnerT>::bind(runtime::RenderSurfaceHandle handle,
                 std::size_t slot,
                 const runtime::SurfaceDesc& desc,
                 std::span<const runtime::RenderSurfaceHandle> planned_occupants ) {
        require_owner();
        return bind_planned_exact(handle, slot, desc, planned_occupants);
    }

template <typename ImageT, typename OwnerT>
ImageT& VulkanSurfaceAuthority<ImageT, OwnerT>::ensure(runtime::RenderSurfaceHandle handle,
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

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::prune_unused_slots() {
        prune_unused_slots_impl(false, kMaxRetainedTransientSlots);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::prune_unused_slots(std::size_t max_retained) {
        prune_unused_slots_impl(false, max_retained);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::reclaim_unused_transient_slots() {
        prune_unused_slots_impl(true, 0);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::prune_unused_slots_impl(bool force, std::size_t max_retained) {
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

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::record_frame_slot_transient(std::size_t slot,
                                     runtime::RenderSurfaceHandle handle) {
        if (slot >= kFrameBatchSlotCount ||
            handle == runtime::kInvalidRenderSurfaceHandle) return;
        auto& list = frame_slot_transient_handles[slot];
        if (std::find(list.begin(), list.end(), handle) == list.end()) list.push_back(handle);
    }

template <typename ImageT, typename OwnerT>
std::size_t VulkanSurfaceAuthority<ImageT, OwnerT>::retire_frame_slot_transients(
        std::size_t slot, std::span<const bool> busy ) {
        if (slot >= kFrameBatchSlotCount) return 0;
        const detail::SurfaceRetirementCallbacks callbacks{
            this,
            [](void* context, std::size_t physical_slot) {
                auto& authority = *static_cast<VulkanSurfaceAuthority*>(context);
                const auto it = authority.physical_surfaces.find(physical_slot);
                if (it != authority.physical_surfaces.end() &&
                    !authority.slot_in_use(physical_slot)) {
                    it->second.image.initialized = false;
                }
            },
            [](void* context, std::size_t limit) {
                static_cast<VulkanSurfaceAuthority*>(context)->prune_unused_slots(limit);
            }};
        return detail::retire_frame_slot_transients(
            surface_bindings, unplanned_surface_handles, recording_slot_masks,
            frame_slot_transient_handles, slot, busy, callbacks,
            kMaxRetainedTransientSlots);
    }

template <typename ImageT, typename OwnerT>
void VulkanSurfaceAuthority<ImageT, OwnerT>::require_owner() {
        if (owner_ == nullptr) {
            throw std::logic_error("Vulkan surface authority has no owner");
        }
        materializer_.set_owner(owner_);
    }

template <typename ImageT, typename OwnerT>
ImageT& VulkanSurfaceAuthority<ImageT, OwnerT>::bind_planned_exact(runtime::RenderSurfaceHandle handle,
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

template <typename ImageT, typename OwnerT>
ImageT& VulkanSurfaceAuthority<ImageT, OwnerT>::bind_unplanned(runtime::RenderSurfaceHandle handle,
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

template <typename ImageT, typename OwnerT>
ImageT& VulkanSurfaceAuthority<ImageT, OwnerT>::materialize_binding(runtime::RenderSurfaceHandle handle,
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
            materializer_.materialize(physical.image, desc, exportable);
        }
        physical.desc = desc;
        physical.image.text_atlas_encoding = desc.text_atlas_encoding;
        surface_bindings[handle] = slot;
        note_binding_created();
        bump_slot_token(slot);
        return physical.image;
    }

template class VulkanSurfaceAuthority<VulkanBackend::Impl::Image, VulkanBackend::Impl>;

} // namespace chronon3d::backends::vulkan
