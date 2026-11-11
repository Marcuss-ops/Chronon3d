// Surface lifetime bookkeeping and retirement policy for the Vulkan backend.
// Kept out of the authority header so its synchronization contract remains
// visible without forcing retirement implementation into every includer.
#include "vulkan_backend_impl.hpp"

#include <algorithm>
#include <span>
#include <vector>
#include <spdlog/spdlog.h>


namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::record_slot_transient_surface(
    std::size_t slot, runtime::RenderSurfaceHandle handle) {
    surfaces.record_frame_slot_transient(slot, handle);
}

void VulkanBackend::Impl::retire_frame_slot_transient_surfaces(std::size_t slot) {
    const auto busy = frame_slot_busy_flags();
    const std::size_t released = surfaces.retire_frame_slot_transients(
        slot, std::span<const bool>(busy));
    if (released == 0) return;
    if (surface_lifecycle_diag_enabled()) {
        spdlog::info("[surface-lifecycle] retire slot={} handles={}", slot, released);
    }
    stats.surface_releases += released;
}

void VulkanBackend::Impl::release_frame_transient_handles(
    bool log_lifecycle,
    std::span<const runtime::RenderSurfaceHandle> protected_handles) {
    // This is the post-fence drain: the frames whose leases protect these
    // images have completed. Drop any residual recording lease before
    // pruning, otherwise slot_pinned() keeps every retired text surface in
    // the physical pool until the whole clip exhausts VRAM.
    release_all_recording_leases_locked();
    // The fence that owns this drain has completed, so any pin still in the
    // table is stale bookkeeping rather than live GPU work. Clear it before
    // the forced reclaim; otherwise every retired 1080p text surface remains
    // "pinned" and the physical pool grows until vmaCreateImage fails.
    surfaces.clear_leases();
    // Reused scratch avoids a per-call handle-vector allocation in cleanup.
    auto& handles = transient_release_scratch_;
    handles.clear();
    handles.reserve(surfaces.surface_bindings.size());
    const auto busy = frame_slot_busy_flags();
    for (const auto& [handle, slot] : surfaces.surface_bindings) {
        const auto it = surfaces.physical_surfaces.find(slot);
        if (it != surfaces.physical_surfaces.end() &&
            it->second.desc.lifetime == runtime::LifetimeClass::FrameTransient &&
            !surfaces.unplanned_surface_handles.contains(handle) &&
            // A slot the CUDA/NVENC consumer still owns is not releasable:
            // unbinding it here would mark the image free for a later
            // re-materialize that destroys memory the consumer maps.
            !surfaces.slot_export_owned(slot) &&
            surfaces.can_retire_surface(handle, busy) &&
            std::find(protected_handles.begin(), protected_handles.end(), handle) ==
                protected_handles.end()) {
            handles.push_back(handle);
        }
    }
    for (const auto handle : handles) {
        if (log_lifecycle && surface_lifecycle_diag_enabled())
            spdlog::info("[surface-lifecycle] retire handle={}", handle);
        const auto binding = surfaces.surface_bindings.find(handle);
        if (binding == surfaces.surface_bindings.end()) continue;
        const auto slot = binding->second;
        surfaces.surface_bindings.erase(binding);
        surfaces.unplanned_surface_handles.erase(handle);
        surfaces.forget_recording_slots(handle);
        const auto it = surfaces.physical_surfaces.find(slot);
        if (it != surfaces.physical_surfaces.end() && !surfaces.slot_in_use(slot))
            it->second.image.initialized = false;
        ++stats.surface_releases;
    }
    // This drain runs only after the owning frame fence has completed, so no
    // recording lease can still pin these images. Force the pool down to the
    // retained bound instead of keeping every retired 1080p transient (a
    // 15 s text clip otherwise accumulates ~280 x 33 MB and exhausts VRAM).
    reclaim_unused_transient_surface_slots();
}

void VulkanBackend::Impl::release_frame_transient_surfaces() noexcept {
    try {
        // Explicit end-of-job drain: wait only for submissions tracked by
        // this backend. Do not use vkDeviceWaitIdle here; the compiled
        // frame path must never acquire an implicit device-global sync.
        wait_for_pending();
        flush_deferred_surface_releases();
        for (auto& handles : surfaces.frame_slot_transient_handles) {
            handles.clear();
        }
        // The tracked-submission drain above makes orphaned transient
        // backing images safe to destroy without a global device idle.
        release_frame_transient_handles(/*log_lifecycle=*/false);
        prune_unused_transient_slots(0);
        if (surfaces.physical_surfaces.size() > 32) {
            // Diagnose why the forced prune retained images. A slot survives
            // for exactly four reasons: job-persistent lifetime, an external
            // consumer handoff that has not returned, a live binding, or a
            // recording pin. Without the split the pool growth is opaque.
            std::size_t persistent = 0, exported = 0, bound = 0, pinned = 0, reclaimable = 0;
            for (const auto& [slot, physical] : surfaces.physical_surfaces) {
                if (physical.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                    ++persistent;
                } else if (physical.image.export_handed_off) {
                    ++exported;
                } else if (surfaces.slot_in_use(slot)) {
                    ++bound;
                } else if (surfaces.slot_pinned(slot)) {
                    ++pinned;
                } else {
                    ++reclaimable;
                }
            }
            spdlog::warn("[vulkan] transient cleanup retained bindings={} surfaces.physical_surfaces={} unplanned={} "
                         "job_persistent={} export_handed_off={} bound={} pinned={} reclaimable={}",
                         surfaces.surface_bindings.size(), surfaces.physical_surfaces.size(),
                         surfaces.unplanned_surface_handles.size(), persistent, exported,
                         bound, pinned, reclaimable);
            std::size_t samples = 0;
            for (const auto& [slot, physical] : surfaces.physical_surfaces) {
                if (samples++ >= 4) break;
                spdlog::warn("[vulkan] retained slot={} {}x{} lifetime={} exportable={} exported={} initialized={} bound={} pinned={}",
                             slot, physical.image.width, physical.image.height,
                             static_cast<int>(physical.desc.lifetime),
                             physical.image.exportable, physical.image.export_handed_off,
                             physical.image.initialized, surfaces.slot_in_use(slot),
                             surfaces.slot_pinned(slot));
            }
        }
    } catch (const std::exception& error) {
        spdlog::error("[vulkan] transient cleanup failed: {}", error.what());
    } catch (...) {
        // Cleanup is best-effort and must not terminate the daemon.
        spdlog::warn("[vulkan] transient cleanup aborted by a non-std exception");
    }
}

void VulkanBackend::Impl::retire_completed_frame_transient_surfaces(
    std::span<const runtime::RenderSurfaceHandle> protected_handles) noexcept {
    try {
        // A transient image is eligible for destruction only after the
        // frame submission that references it has completed. Poll the frame
        // fences instead of draining every pending submission: a blocking
        // wait here would serialize the Vulkan->CUDA/NVENC pipeline.
        for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
            if (!frame_batch.in_flight[i]) continue;
            const auto status = vkGetFenceStatus(device, frame_batch.fences[i]);
            if (status == VK_NOT_READY) continue;
            check(status, "vkGetFenceStatus(frame transient retirement)");
            check(vkResetFences(device, 1, &frame_batch.fences[i]),
                  "vkResetFences(frame transient retirement)");
            frame_batch.in_flight[i] = false;
            read_gpu_timestamps(i);
            // Retire only handles materialized inside this completed slot.
            retire_frame_slot_transient_surfaces(i);
        }
        // Preserve host-provided sources: release only after a frame that
        // referenced a transient handle has actually completed.
        release_frame_transient_handles(/*log_lifecycle=*/true, protected_handles);
    } catch (const std::exception& error) {
        spdlog::error("[vulkan] non-blocking transient retirement failed: {}", error.what());
    } catch (...) {
        // Final job cleanup remains responsible for recovery.
        spdlog::warn("[vulkan] non-blocking transient retirement aborted by a non-std exception");
    }
}

} // namespace chronon3d::backends::vulkan
