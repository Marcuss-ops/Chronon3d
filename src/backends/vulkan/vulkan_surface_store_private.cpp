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

    void VulkanBackend::Impl::wait_upload_slot(VulkanUploadRing::UploadSlot& slot) {
        if (!slot.in_flight) return;
        check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences(upload slot)");
        check(vkResetFences(device, 1, &slot.fence), "vkResetFences(upload slot)");
        slot.in_flight = false;
    }

    VulkanBackend::Impl::VulkanUploadRing::UploadSlot& VulkanBackend::Impl::acquire_upload_slot(bool wait_for_completion) {
        // Synchronous callers retain the warmed first slot.  Asynchronous
        // callers rotate through the ring and only wait when all slots are
        // occupied, allowing several decoded assets to be queued together.
        if (wait_for_completion) {
            wait_upload_slot(uploads.slots[0]);
            return uploads.slots[0];
        }
        for (std::size_t attempt = 0; attempt < VulkanUploadRing::kSlotCount; ++attempt) {
            auto& slot = uploads.slots[uploads.next_slot];
            uploads.next_slot = (uploads.next_slot + 1) % VulkanUploadRing::kSlotCount;
            if (!slot.in_flight) return slot;
            if (vkGetFenceStatus(device, slot.fence) == VK_SUCCESS) {
                wait_upload_slot(slot);
                return slot;
            }
        }
        auto& oldest = uploads.slots[uploads.next_slot];
        wait_upload_slot(oldest);
        uploads.next_slot = (uploads.next_slot + 1) % VulkanUploadRing::kSlotCount;
        return oldest;
    }

    void VulkanBackend::Impl::ensure_upload_slot(VulkanUploadRing::UploadSlot& slot, VkDeviceSize bytes) {
        wait_upload_slot(slot);
        if (slot.buffer_allocation.size < bytes) {
            if (slot.buffer_allocation.buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(slot.buffer_allocation);
            }
            const VkBufferCreateInfo info{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
            slot.buffer_allocation = memory_manager.create_buffer(info, VulkanMemoryClass::HostUpload);
            if (debug_context) debug_context->set_buffer_name(slot.buffer_allocation.buffer, "Chronon3D.Buffer.SurfaceUpload");
            ++stats.staging_allocations;
        }
        if (slot.command_buffer == VK_NULL_HANDLE) {
            const VkCommandBufferAllocateInfo command_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
            check(vkAllocateCommandBuffers(device, &command_info, &slot.command_buffer),
                  "vkAllocateCommandBuffers(upload slot)");
            if (debug_context) debug_context->set_command_buffer_name(slot.command_buffer, "Chronon3D.CommandBuffer.SurfaceUploadSlot");
            const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            check(vkCreateFence(device, &fence_info, nullptr, &slot.fence),
                  "vkCreateFence(upload slot)");
            if (debug_context) debug_context->set_fence_name(slot.fence, "Chronon3D.Fence.SurfaceUploadSlot");
        }
    }

    void VulkanBackend::Impl::clear_uninitialized_upload_target(
        VkCommandBuffer command, Image& image) {
        if (image.initialized) return;

        // A retained physical slot can still contain the previous occupant's
        // pixels when its logical binding is recycled. Partial uploads must
        // establish transparent contents before copying their first region.
        transition(command, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue clear{{0.0f, 0.0f, 0.0f, 0.0f}};
        const VkImageSubresourceRange range{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(command, image.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &range);
        // The first partial/full upload follows the clear with another transfer
        // write to this subresource in the same command buffer. Layout remains
        // TRANSFER_DST_OPTIMAL, so express the transfer-write dependency
        // explicitly before vkCmdCopyBufferToImage.
        runtime::ResourceTransition clear_to_upload;
        clear_to_upload.range =
            runtime::image_range(runtime::ResourceAspect::Color);
        clear_to_upload.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::Transfer,
            .access = runtime::AccessMask::TransferWrite,
            .layout = runtime::ResourceLayout::TransferDestination,
            .queue = runtime::QueueClass::Transfer,
        };
        clear_to_upload.after = clear_to_upload.before;
        emit_resource_transition(command, image.image, clear_to_upload);
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

    void VulkanBackend::Impl::record_slot_transient_surface(
        std::size_t slot, runtime::RenderSurfaceHandle handle) {
        // The authority owns the per-slot record and its retirement policy;
        // this method is only the backend-side entry point.
        surfaces.record_frame_slot_transient(slot, handle);
    }

    void VulkanBackend::Impl::retire_frame_slot_transient_surfaces(std::size_t slot) {
        const auto busy = frame_slot_busy_flags();
        const std::size_t released = surfaces.retire_frame_slot_transients(
            slot, std::span<const bool>(busy));
        if (released == 0) return;
        if (surface_lifecycle_diag_enabled()) {
            spdlog::info("[surface-lifecycle] retire slot={} handles={}",
                         slot, released);
        }
        stats.surface_releases += released;
    }

    void VulkanBackend::Impl::release_frame_transient_handles(
        bool log_lifecycle,
        std::span<const runtime::RenderSurfaceHandle> protected_handles) {
        // Reused scratch (see transient_release_scratch_): avoids a
        // per-call handle-vector allocation in the cleanup drains.
        auto& handles = transient_release_scratch_;
        handles.clear();
        handles.reserve(surfaces.surface_bindings.size());
        const auto busy = frame_slot_busy_flags();
        for (const auto& [handle, slot] : surfaces.surface_bindings) {
            const auto it = surfaces.physical_surfaces.find(slot);
            if (it != surfaces.physical_surfaces.end() &&
                it->second.desc.lifetime == runtime::LifetimeClass::FrameTransient &&
                !surfaces.unplanned_surface_handles.contains(handle) &&
                surfaces.can_retire_surface(handle, busy) &&
                std::find(protected_handles.begin(), protected_handles.end(), handle) ==
                    protected_handles.end()) {
                handles.push_back(handle);
            }
        }
        for (const auto handle : handles) {
            if (log_lifecycle && surface_lifecycle_diag_enabled()) {
                spdlog::info("[surface-lifecycle] retire handle={}", handle);
            }
            const auto binding = surfaces.surface_bindings.find(handle);
            if (binding == surfaces.surface_bindings.end()) continue;
            const auto slot = binding->second;
            surfaces.surface_bindings.erase(binding);
            surfaces.unplanned_surface_handles.erase(handle);
            surfaces.forget_recording_slots(handle);
            const auto it = surfaces.physical_surfaces.find(slot);
            if (it != surfaces.physical_surfaces.end() &&
                !surfaces.slot_in_use(slot)) {
                it->second.image.initialized = false;
            }
            ++stats.surface_releases;
        }
        prune_unused_transient_slots();
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
                spdlog::warn("[vulkan] transient cleanup retained bindings={} surfaces.physical_surfaces={} unplanned={}",
                             surfaces.surface_bindings.size(), surfaces.physical_surfaces.size(),
                             surfaces.unplanned_surface_handles.size());
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
            // frame submission that references it has completed.  Poll the
            // frame fences instead of draining every pending submission: a
            // blocking wait here serialized the Vulkan->CUDA/NVENC pipeline
            // and made every exported frame pay the full GPU latency.
            for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
                if (!frame_batch.in_flight[i]) continue;
                const auto status = vkGetFenceStatus(device, frame_batch.fences[i]);
                if (status == VK_NOT_READY) continue;
                check(status, "vkGetFenceStatus(frame transient retirement)");
                check(vkResetFences(device, 1, &frame_batch.fences[i]),
                      "vkResetFences(frame transient retirement)");
                frame_batch.in_flight[i] = false;
                read_gpu_timestamps(i);
                // Per-frame-slot retirement first: scoped to handles recorded
                // inside this batch slot, so host surfaces created outside a
                // batch are never touched (the failure mode of the earlier
                // global retire).  Keeps the gl-lifecycle bound at 8 slots.
                retire_frame_slot_transient_surfaces(i);
            }

            // Reclaim only once a frame that referenced these transient handles
            // has actually completed. When nothing is in flight there is no
            // completed frame yet, and retiring here would erase host-provided
            // sources that were created/uploaded before the first batch — the
            // image survives, but its `initialized` flag would be cleared
            // before its first use, and re-binding with a matching desc does
            // not re-materialize it.
            // All relevant submission fences were complete above, so it is
            // now safe to reclaim the physical images whose logical handles
            // are retired below.
            release_frame_transient_handles(/*log_lifecycle=*/true, protected_handles);
        } catch (const std::exception& error) {
            spdlog::error("[vulkan] non-blocking transient retirement failed: {}", error.what());
        } catch (...) {
            // Best effort: final job cleanup remains responsible for recovery.
            spdlog::warn("[vulkan] non-blocking transient retirement aborted by a non-std exception");
        }
    }

    std::uint64_t VulkanBackend::Impl::submit_upload(VulkanUploadRing::UploadSlot& slot, bool wait_for_completion) {
        check(vkEndCommandBuffer(slot.command_buffer), "vkEndCommandBuffer(upload slot)");
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSemaphore signal_semaphores[] = {timeline_semaphore};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit, 0, nullptr, nullptr,
            1, &slot.command_buffer, 1, signal_semaphores};
        check(vkQueueSubmit(queue, 1, &submit_info, slot.fence),
              "vkQueueSubmit(upload slot)");
        ++stats.submissions;
        slot.ticket = signal_value;
        slot.in_flight = true;
        if (wait_for_completion) wait_upload_slot(slot);
        return signal_value;
    }

    std::uint64_t VulkanBackend::Impl::upload(runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
                         std::span<const float> rgba, bool wait_for_completion) {
        auto& image = ensure_surface(handle, desc);
        // Full uploads use the descriptor's actual storage format.  The old
        // hard-coded Rgba32Float size rejected persistent Rgba8 MTSDF pages
        // before their first glyph region upload could even be issued.
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(runtime::tight_surface_bytes(
            desc.format, desc.width, desc.height));
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan upload size does not match surface");
        ++stats.upload_calls;
        stats.upload_bytes += bytes;
        stats.upload_full_surface_bytes += bytes;
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += bytes;
            stats.upload_producer_full_count[producer] += 1;
            if (profiling::g_gpu_upload_producer == profiling::GpuUploadProducer::Image) {
                stats.upload_producer_initial_count[producer] += 1;
                stats.upload_producer_initial_bytes[producer] += bytes;
            }
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, bytes);
        std::memcpy(slot.buffer_allocation.mapped, rgba.data(), static_cast<std::size_t>(bytes));
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0),
              "vkResetCommandBuffer(upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(upload slot)");
        clear_uninitialized_upload_target(slot.command_buffer, image);
        if (image.initialized) {
            transition(slot.command_buffer, image.image,
                       VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        const VkBufferImageCopy copy{0, desc.width, desc.height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {desc.width, desc.height, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer_allocation.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        const auto ticket = submit_upload(slot, wait_for_completion);
        return ticket;
    }

    std::uint64_t VulkanBackend::Impl::upload_region(runtime::RenderSurfaceHandle handle,
                                const runtime::SurfaceDesc& desc,
                                std::int32_t x, std::int32_t y,
                                std::uint32_t width, std::uint32_t height,
                                std::span<const float> rgba,
                                bool wait_for_completion) {
        if (x < 0 || y < 0 || width == 0 || height == 0 ||
            static_cast<std::uint32_t>(x) + width > desc.width ||
            static_cast<std::uint32_t>(y) + height > desc.height) {
            throw std::invalid_argument("Vulkan region upload is outside surface bounds");
        }
        auto& image = ensure_surface(handle, desc);
        const auto px_bytes = pixel_format_bytes(desc.format);
        const VkDeviceSize byte_count = static_cast<VkDeviceSize>(width) * height * px_bytes;
        const VkDeviceSize src_byte_count = static_cast<VkDeviceSize>(rgba.size_bytes());
        if (desc.format == runtime::PixelFormat::R8Unorm) {
            // R8Unorm: one float per pixel (coverage 0..1), packed to 1 byte.
            // Accept the float span and pack during upload.
            const auto expected_floats = static_cast<VkDeviceSize>(width) * height;
            if (static_cast<VkDeviceSize>(rgba.size()) != expected_floats) {
                throw std::invalid_argument(
                    "Vulkan region upload size does not match rectangle for R8Unorm");
            }
        } else if (src_byte_count != byte_count) {
            throw std::invalid_argument("Vulkan region upload size does not match rectangle");
        }
        ++stats.upload_calls;
        stats.upload_bytes += byte_count;
        stats.upload_region_bytes += byte_count;
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += byte_count;
            stats.upload_producer_region_count[producer] += 1;
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, byte_count);
        if (desc.format == runtime::PixelFormat::R8Unorm) {
            // Pack float→R8: clamp to [0,1], convert to uint8
            auto* dst = static_cast<std::uint8_t*>(slot.buffer_allocation.mapped);
            for (std::size_t i = 0; i < rgba.size(); ++i) {
                dst[i] = static_cast<std::uint8_t>(
                    std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        } else {
            std::memcpy(slot.buffer_allocation.mapped, rgba.data(), static_cast<std::size_t>(byte_count));
        }
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0), "vkResetCommandBuffer(region upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin), "vkBeginCommandBuffer(region upload slot)");
        clear_uninitialized_upload_target(slot.command_buffer, image);
        if (image.initialized) {
            transition(slot.command_buffer, image.image,
                       VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        const VkBufferImageCopy copy{
            0, width, height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {x, y, 0}, {width, height, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer_allocation.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }

    std::uint64_t VulkanBackend::Impl::upload_rows(
        runtime::RenderSurfaceHandle handle,
        const runtime::SurfaceDesc& desc,
        const raster::BBox& clip,
        const graph::RenderBackend::UploadRows& rows,
        bool wait_for_completion) {
        if (clip.x0 < 0 || clip.y0 < 0 || clip.x1 <= clip.x0 || clip.y1 <= clip.y0 ||
            static_cast<std::uint32_t>(clip.x1) > desc.width ||
            static_cast<std::uint32_t>(clip.y1) > desc.height) {
            throw std::invalid_argument("Vulkan rows upload is outside surface bounds");
        }
        const auto rw = static_cast<std::uint32_t>(clip.x1 - clip.x0);
        const auto rh = static_cast<std::uint32_t>(clip.y1 - clip.y0);
        if (rows.first_row.size() < rw || rows.stride < rw) {
            throw std::invalid_argument("Vulkan rows upload span or stride smaller than clip width");
        }
        auto& image = ensure_surface(handle, desc);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(rw) * rh * sizeof(Color);
        const bool full_upload = (clip.x0 == 0 && clip.y0 == 0 &&
                                  rw == desc.width && rh == desc.height);
        ++stats.upload_calls;
        stats.upload_bytes += bytes;
        if (full_upload) {
            stats.upload_full_surface_bytes += bytes;
        } else {
            stats.upload_region_bytes += bytes;
        }
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += bytes;
            if (full_upload) {
                stats.upload_producer_full_count[producer] += 1;
            } else {
                stats.upload_producer_region_count[producer] += 1;
            }
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, bytes);
        auto* dst = static_cast<char*>(slot.buffer_allocation.mapped);
        const auto* src = reinterpret_cast<const char*>(rows.first_row.data());
        const std::size_t row_bytes = rw * sizeof(Color);
        const std::size_t stride_bytes = rows.stride * sizeof(Color);
        if (rows.stride == rw) {
            std::memcpy(dst, src, row_bytes * rh);
        } else {
            for (std::size_t y = 0; y < rh; ++y) {
                std::memcpy(dst + y * row_bytes, src + y * stride_bytes, row_bytes);
            }
        }
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0),
              "vkResetCommandBuffer(rows upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(rows upload slot)");
        transition(slot.command_buffer, image.image,
                   image.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy copy{0, rw, rh,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {clip.x0, clip.y0, 0},
            {rw, rh, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer_allocation.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }

    void VulkanBackend::Impl::wait_upload_ticket(std::uint64_t ticket) {
        const VkSemaphoreWaitInfo wait_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1,
            &timeline_semaphore, &ticket};
        check(vkWaitSemaphores(device, &wait_info, UINT64_MAX),
              "vkWaitSemaphores(upload ticket)");
        for (auto& slot : uploads.slots) {
            if (slot.in_flight && slot.ticket == ticket) wait_upload_slot(slot);
        }
    }

    void VulkanBackend::Impl::download(runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
        if (surfaces.surface_bindings.count(handle) == 0) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        auto& image = resolve_image(handle);
        if (!image.initialized) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(runtime::tight_surface_bytes(
            runtime::PixelFormat::Rgba32Float, image.width, image.height));
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan download size does not match surface");
        ++stats.readback_calls;
        stats.readback_bytes += bytes;
        // Gate 7 (cpu_pixel_readback_bytes) canary — this function is the
        // single GPU→CPU pixel download authority on the Vulkan backend.
        // Instrumented here (2026-09-07, Z0 census) so the zero-copy gate is
        // measured instead of passing vacuously; the native video lane never
        // reaches download() (mark_gpu_authoritative + refusal paths), so a
        // non-zero value is an honest video regression canary.
        if (auto* counters = profiling::g_current_counters) {
            counters->cpu_pixel_readback_count.fetch_add(
                1, std::memory_order_relaxed);
            counters->cpu_pixel_readback_bytes.fetch_add(
                static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
        }
        ensure_staging(bytes);
        begin_command_buffer();
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy copy{0, image.width, image.height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {image.width, image.height, 1}};
        vkCmdCopyImageToBuffer(command_buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        submit();
        const auto readback_start = profiling::now();
        std::memcpy(rgba.data(), staging.mapped, static_cast<std::size_t>(bytes));
        stats.readback_us += static_cast<std::uint64_t>(profiling::elapsed_us(readback_start));
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
