// vulkan_surface_store_private.cpp — VulkanBackend::Impl surface
// ownership, upload/download and transient-slot lifecycle.
// Out-of-class definitions; declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

#include <vector>

namespace chronon3d::backends::vulkan {

    // DEMOLISHED (P1.4): Impl::preallocate_plan_surfaces removed — it never
    // preallocated anything.  The prune it performed lives on through
    // prune_unused_transient_slots(), and native surface materialization is
    // the lazy VulkanSurfaceAuthority path below.

    // Compatibility façade: all surface slot/binding policy lives in
    // VulkanSurfaceAuthority. Impl only delegates so there is one authority.
    bool VulkanBackend::Impl::slot_in_use(std::size_t slot) const {
        return surfaces.slot_in_use(slot);
    }

    void VulkanBackend::Impl::prune_unused_transient_slots(std::size_t max_retained) {
        surfaces.prune_unused_slots(max_retained);
    }

    std::size_t VulkanBackend::Impl::bound_slot(
        runtime::RenderSurfaceHandle handle) const {
        return surfaces.bound_slot(handle);
    }

    VulkanBackend::Impl::Image& VulkanBackend::Impl::resolve_image(
        runtime::RenderSurfaceHandle handle) {
        // Every recorded command resolves its surfaces here, so this is the one
        // place that captures which frame-batch slot references a handle. The
        // recording path holds the submission domain, which orders this write
        // against the release/retire readers (both hold both domains).
        if (frame_batch.active && frame_batch.next_slot < FrameBatchState::kSlotCount) {
            surfaces.note_recording_slot(handle, frame_batch.next_slot);
        }
        // Audit N4: during recording this thread holds no store lock, so the
        // image comes from the recording lease, which pins the slot against
        // erase/re-materialization. A recording op that reaches a surface it
        // never leased must fail loudly: reading the unlocked store map here
        // would be a data race, not a fallback.
        //
        // Outside a recording window the caller owns the store lock (all other
        // call sites are under surfaces.mutex, e.g. download_surface) and reads
        // the store directly, even while plan-wide leases are pinned.
        if (!recording_resolve_context()) {
            if (frame_batch.active)
                acquire_cuda_surface_for_recording(active_command_buffer(),
                                                   bound_slot(handle));
            return surfaces.resolve(handle);
        }
        if (auto* leased = leased_image(handle)) return *leased;
        throw std::logic_error(
            "Vulkan recording resolved an unpinned surface " +
            std::to_string(handle));
    }

    bool VulkanBackend::Impl::slot_has_initialized_occupant(
        std::size_t slot, runtime::RenderSurfaceHandle self) const {
        return surfaces.slot_has_initialized_occupant(slot, self);
    }

    VulkanBackend::Impl::Image& VulkanBackend::Impl::bind_handle_to_slot(
        runtime::RenderSurfaceHandle handle,
        std::size_t slot,
        const runtime::SurfaceDesc& desc) {
        return surfaces.bind(handle, slot, desc);
    }

    VulkanBackend::Impl::Image& VulkanBackend::Impl::ensure_surface(
        runtime::RenderSurfaceHandle handle,
        const runtime::SurfaceDesc& desc) {
        auto& image = surfaces.ensure(handle, desc);
        if (desc.lifetime == runtime::LifetimeClass::FrameTransient &&
            frame_batch.active && frame_batch.next_slot < FrameBatchState::kSlotCount) {
            record_slot_transient_surface(frame_batch.next_slot, handle);
        }
        return image;
    }

    void VulkanBackend::Impl::release_surface_now(runtime::RenderSurfaceHandle handle) {
        if (surface_lifecycle_diag_enabled()) {
            spdlog::info("[surface-lifecycle] release-now handle={}", handle);
        }
        const auto binding = surfaces.surface_bindings.find(handle);
        if (binding == surfaces.surface_bindings.end()) return;
        // Capture the slot BEFORE the wait.  wait_for_pending() retires the
        // completed frame-batch slots' transient surfaces, and that retirement
        // ERASES entries from surface_bindings.  Holding the iterator across
        // the wait made `binding` dangle and the erase below ran on a stale
        // node (SIGSEGV inside std::_Hashtable::erase, reproduced on the CLI
        // Vulkan export at the warmup frame).  Erasing by KEY afterwards is
        // safe whether or not the wait already retired this very binding.
        const auto slot = binding->second;
        wait_for_pending();
        if (surfaces.surface_bindings.erase(handle) == 0) return;
        surfaces.forget_recording_slots(handle);
        // Retain surfaces.physical_surfaces[slot] so subsequent ensure_surface calls
        // can reuse the allocated physical surface without re-allocating memory!
        surfaces.unplanned_surface_handles.erase(handle);
        if (surfaces.surface_bindings.empty()) {
            // Once the last logical handle is gone there is no warm pool to
            // preserve: tests and teardown rely on the physical image count
            // reaching zero, and no future binding can reuse this batch's
            // retired content safely.
            surfaces.reclaim_unused_transient_slots();
        } else {
            prune_unused_transient_slots();
        }
        ++stats.surface_releases;
    }

    void VulkanBackend::Impl::flush_deferred_surface_releases() {
        if (surfaces.deferred_surface_releases.empty()) return;
        ++stats.deferred_release_flush_count;
        const auto flush_start = profiling::now();

        // Non-blocking fence polling: retire slots that have completed on GPU
        for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
            if (!frame_batch.in_flight[i]) continue;
            const auto status = vkGetFenceStatus(device, frame_batch.fences[i]);
            if (status == VK_NOT_READY) continue;
            check(status, "vkGetFenceStatus(deferred release polling)");
            check(vkResetFences(device, 1, &frame_batch.fences[i]),
                  "vkResetFences(deferred release polling)");
            frame_batch.in_flight[i] = false;
            read_gpu_timestamps(i);
            retire_frame_slot_transient_surfaces(i);
        }

        // Bounded capacity: fallback wait if accumulated deferred releases exceed cap
        static constexpr std::size_t kOverflowCap = 2 * FrameBatchState::kSlotCount;
        if (surfaces.note_overflow_if_needed(kOverflowCap)) {
            wait_for_pending();
        }

        auto pending = std::move(surfaces.deferred_surface_releases);
        surfaces.deferred_surface_releases.clear();
        surfaces.deferred_surface_releases.reserve(pending.size());

        const auto busy = frame_slot_busy_flags();
        for (const auto& item : pending) {
            // Gate on every slot that referenced the handle, not on one guess:
            // a release while any of them is in flight could free an image
            // that submission still reads.
            if (!recording_slots_retired(item.slot_mask, busy)) {
                surfaces.enqueue_deferred_release(item);
                continue;
            }
            if (surface_lifecycle_diag_enabled()) {
                spdlog::info("[surface-lifecycle] deferred-release handle={} slot={}",
                             item.handle, item.slot);
            }
            const auto binding = surfaces.surface_bindings.find(item.handle);
            if (binding == surfaces.surface_bindings.end()) continue;
            const auto slot = binding->second;
            if (surfaces.surface_bindings.erase(item.handle) == 0) continue;
            surfaces.forget_recording_slots(item.handle);
            surfaces.note_release_delay(item.requested_frame_index,
                                         active_frame_index);
            surfaces.unplanned_surface_handles.erase(item.handle);
            if (surfaces.surface_bindings.empty()) {
                surfaces.reclaim_unused_transient_slots();
            } else {
                prune_unused_transient_slots();
            }
            ++stats.surface_releases;
        }

        const auto flush_us = static_cast<std::uint64_t>(profiling::elapsed_us(flush_start));
        stats.deferred_release_drain_wait_us += flush_us;
    }

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
    planned_alias_groups.clear();
    deferred_surface_releases.clear();
    recording_slot_masks.clear();
    unplanned_surface_handles.clear();
    for (auto& handles : frame_slot_transient_handles) {
        handles.clear();
    }
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    cuda_pending_surfaces.clear();
    cuda_ready_surfaces.clear();
    cuda_export_ready_surfaces.clear();
#endif
    // Teardown happens after the device idle point and with no recording in
    // flight; draining the pin table here keeps the invariant observable.
    clear_leases();
    next_slot = 0;
}

} // namespace chronon3d::backends::vulkan
