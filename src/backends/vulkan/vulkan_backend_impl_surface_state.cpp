#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::require_healthy() const {
    runtime::require_healthy_gpu_worker();
}

void VulkanBackend::Impl::destroy_surface_images() noexcept {
    surfaces.destroy_all(*this); 
}

void VulkanBackend::Impl::defer_surface_release(runtime::RenderSurfaceHandle handle) {
    ++stats.deferred_surface_release_count;
    // Exact owner set: every frame-batch slot whose recorded commands resolved
    // this handle since its last release (captured in resolve_image). A handle
    // that was never recorded falls back to the slot being recorded now, which
    // can only delay the release, never free early.
    std::uint8_t mask = surfaces.recording_slot_mask(handle);
    if (mask == 0 && frame_batch.next_slot < FrameBatchState::kSlotCount) {
        mask = frame_slot_bit(frame_batch.next_slot);
    }
    std::size_t slot = 0;
    for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
        if ((mask & frame_slot_bit(i)) != 0) slot = i;
    }
    surfaces.enqueue_deferred_release(
        {handle, slot, active_frame_index, mask});
}

std::array<bool, VulkanBackend::Impl::FrameBatchState::kSlotCount>
VulkanBackend::Impl::frame_slot_busy_flags() const {
    auto busy = frame_batch.in_flight;
    if (frame_batch.active && frame_batch.next_slot < FrameBatchState::kSlotCount) {
        // The slot being recorded has no fence yet, but its commands still
        // reference the surfaces it resolved.
        busy[frame_batch.next_slot] = true;
    }
    return busy;
}

bool VulkanBackend::Impl::surface_valid(runtime::RenderSurfaceHandle handle) const noexcept {
    return surfaces.valid(handle);
}

std::size_t VulkanBackend::Impl::surface_physical_count() const noexcept {
    return surfaces.physical_count();
}

bool VulkanBackend::Impl::surface_is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
    return surfaces.is_job_persistent(handle);
}

void VulkanBackend::Impl::prune_surface_slots() {
    surfaces.prune_unused_slots(); 
}

void VulkanBackend::Impl::reclaim_unused_transient_surface_slots() {
    surfaces.reclaim_unused_transient_slots();
}

void VulkanBackend::Impl::pin_recording_leases(
        std::span<const runtime::RenderSurfaceHandle> handles) {
    // Surface domain only: this is the whole point of the N4 decoupling —
    // the recording path never takes it together with the submission ring.
    DomainLock store_lock(*this, LockDomain::Surface);
    pin_recording_leases_locked(handles);
}

void VulkanBackend::Impl::record_lock_wait(VulkanBackend::Impl::LockDomain domain, std::uint64_t microseconds) noexcept {
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

void VulkanBackend::Impl::record_lock_hold(VulkanBackend::Impl::LockDomain domain, std::uint64_t microseconds) noexcept {
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

void VulkanBackend::Impl::rollback_pins_locked(
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

void VulkanBackend::Impl::pin_recording_leases_locked(
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

void VulkanBackend::Impl::release_recording_leases(
        std::span<const runtime::RenderSurfaceHandle> handles) {
    DomainLock store_lock(*this, LockDomain::Surface);
    release_recording_leases_locked(handles);
}

void VulkanBackend::Impl::release_recording_leases_locked(
        std::span<const runtime::RenderSurfaceHandle> handles) {
    std::lock_guard pin_lock(recording_leases_mutex);
    for (const auto handle : handles) {
        if (handle == runtime::kInvalidRenderSurfaceHandle) continue;
        release_lease_entry_locked(handle);
    }
    recording_leases_active.store(!recording_leases.empty(),
                                  std::memory_order_release);
}

void VulkanBackend::Impl::release_lease_entry_locked(runtime::RenderSurfaceHandle handle) {
    const auto it = recording_leases.find(handle);
    if (it == recording_leases.end()) return;
    if (it->second.second > 1) {
        --it->second.second;
        return;
    }
    surfaces.release_lease(it->second.first);
    recording_leases.erase(it);
}

void VulkanBackend::Impl::release_all_recording_leases() {
    DomainLock store_lock(*this, LockDomain::Surface);
    release_all_recording_leases_locked();
}

void VulkanBackend::Impl::release_all_recording_leases_locked() {
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

bool& VulkanBackend::Impl::recording_resolve_context() noexcept {
    static thread_local bool value = false;
    return value;
}

VulkanBackend::Impl::Image* VulkanBackend::Impl::leased_image(runtime::RenderSurfaceHandle handle) {
    if (!recording_leases_active.load(std::memory_order_acquire)) {
        return nullptr;
    }
    std::lock_guard pin_lock(recording_leases_mutex);
    const auto it = recording_leases.find(handle);
    if (it == recording_leases.end()) return nullptr;
    if (frame_batch.active)
        acquire_cuda_surface_for_recording(active_command_buffer(),
                                           it->second.first.slot);
    return it->second.first.image;
}

bool VulkanBackend::Impl::revalidate_recording_leases() const {
    std::lock_guard pin_lock(recording_leases_mutex);
    for (const auto& entry : recording_leases) {
        if (!surfaces.revalidate(entry.second.first)) return false;
    }
    return true;
}

} // namespace chronon3d::backends::vulkan
