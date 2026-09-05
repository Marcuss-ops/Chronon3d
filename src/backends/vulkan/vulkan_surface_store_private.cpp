// vulkan_surface_store_private.cpp — VulkanBackend::Impl surface
// ownership, upload/download and transient-slot lifecycle.
// Out-of-class definitions; declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

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

    void VulkanBackend::Impl::prune_unused_transient_slots() {
        surfaces.prune_unused_slots();
    }

    std::size_t VulkanBackend::Impl::bound_slot(
        runtime::RenderSurfaceHandle handle) const {
        return surfaces.bound_slot(handle);
    }

    VulkanBackend::Impl::Image& VulkanBackend::Impl::resolve_image(
        runtime::RenderSurfaceHandle handle) {
        return surfaces.resolve(handle);
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
        return surfaces.ensure(handle, desc);
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

    void VulkanBackend::Impl::release_surface_now(runtime::RenderSurfaceHandle handle) {
        if (surface_lifecycle_diag_enabled()) {
            spdlog::info("[surface-lifecycle] release-now handle={}", handle);
        }
        const auto binding = surfaces.surface_bindings.find(handle);
        if (binding == surfaces.surface_bindings.end()) return;
        wait_for_pending();
        const auto slot = binding->second;
        surfaces.surface_bindings.erase(binding);
        // Retain surfaces.physical_surfaces[slot] so subsequent ensure_surface calls
        // can reuse the allocated physical surface without re-allocating memory!
        surfaces.unplanned_surface_handles.erase(handle);
        prune_unused_transient_slots();
        ++stats.surface_releases;
    }

    void VulkanBackend::Impl::flush_deferred_surface_releases() {
        if (surfaces.deferred_surface_releases.empty()) return;
        auto pending = std::move(surfaces.deferred_surface_releases);
        surfaces.deferred_surface_releases.clear();
        for (const auto handle : pending) release_surface_now(handle);
    }

    void VulkanBackend::Impl::release_frame_transient_surfaces() noexcept {
        try {
            // Explicit end-of-job drain: wait only for submissions tracked by
            // this backend. Do not use vkDeviceWaitIdle here; the compiled
            // frame path must never acquire an implicit device-global sync.
            wait_for_pending();
            std::vector<runtime::RenderSurfaceHandle> handles;
            handles.reserve(surfaces.surface_bindings.size());
            for (const auto& [handle, slot] : surfaces.surface_bindings) {
                const auto it = surfaces.physical_surfaces.find(slot);
                if (it != surfaces.physical_surfaces.end() &&
                    it->second.desc.lifetime == runtime::LifetimeClass::FrameTransient &&
                    !surfaces.unplanned_surface_handles.contains(handle)) {
                    handles.push_back(handle);
                }
            }
            for (const auto handle : handles) {
                const auto binding = surfaces.surface_bindings.find(handle);
                if (binding == surfaces.surface_bindings.end()) continue;
                const auto slot = binding->second;
                surfaces.surface_bindings.erase(binding);
                surfaces.unplanned_surface_handles.erase(handle);
                const auto it = surfaces.physical_surfaces.find(slot);
                if (it != surfaces.physical_surfaces.end()) {
                    it->second.image.initialized = false;
                }
                ++stats.surface_releases;
            }
            // The tracked-submission drain above makes orphaned transient
            // backing images safe to destroy without a global device idle.
            prune_unused_transient_slots();
            if (surfaces.physical_surfaces.size() > 32) {
                spdlog::warn("[vulkan] transient cleanup retained bindings={} surfaces.physical_surfaces={} unplanned={}",
                             surfaces.surface_bindings.size(), surfaces.physical_surfaces.size(),
                             surfaces.unplanned_surface_handles.size());
            }
        } catch (const std::exception& error) {
            spdlog::error("[vulkan] transient cleanup failed: {}", error.what());
        } catch (...) {
            // Cleanup is best-effort and must not terminate the daemon.
        }
    }

    void VulkanBackend::Impl::retire_completed_frame_transient_surfaces() noexcept {
        try {
            // A transient image is eligible for destruction only after every
            // tracked submission that could reference it has completed.  The
            // old status-only probe could defer cleanup for the whole render
            // job when the encoder kept one upload in flight, accumulating a
            // canvas-sized RGBA32F image per frame.  Drain the backend-owned
            // submission rings here, then reclaim all generic transient
            // bindings in one centralized place.
            wait_for_pending();

            std::vector<runtime::RenderSurfaceHandle> handles;
            handles.reserve(surfaces.surface_bindings.size());
            for (const auto& [handle, slot] : surfaces.surface_bindings) {
                const auto it = surfaces.physical_surfaces.find(slot);
                if (it != surfaces.physical_surfaces.end() &&
                    it->second.desc.lifetime == runtime::LifetimeClass::FrameTransient &&
                    !surfaces.unplanned_surface_handles.contains(handle)) {
                    handles.push_back(handle);
                }
            }
            for (const auto handle : handles) {
                if (surface_lifecycle_diag_enabled()) {
                    spdlog::info("[surface-lifecycle] retire handle={}", handle);
                }
                const auto binding = surfaces.surface_bindings.find(handle);
                if (binding == surfaces.surface_bindings.end()) continue;
                const auto slot = binding->second;
                surfaces.surface_bindings.erase(binding);
                surfaces.unplanned_surface_handles.erase(handle);
                const auto it = surfaces.physical_surfaces.find(slot);
                if (it != surfaces.physical_surfaces.end()) {
                    it->second.image.initialized = false;
                }
                ++stats.surface_releases;
            }
            // All relevant submission fences were complete above, so it is
            // now safe to reclaim the physical images whose logical handles
            // were retired.
            prune_unused_transient_slots();
        } catch (const std::exception& error) {
            spdlog::error("[vulkan] non-blocking transient retirement failed: {}", error.what());
        } catch (...) {
            // Best effort: final job cleanup remains responsible for recovery.
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
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(runtime::tight_surface_bytes(
            runtime::PixelFormat::Rgba32Float, desc.width, desc.height));
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
        transition(slot.command_buffer, image.image,
                   image.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
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
        transition(slot.command_buffer, image.image,
                   image.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
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
    deferred_surface_releases.clear();
    unplanned_surface_handles.clear();
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    cuda_ready_surfaces.clear();
    cuda_export_ready_surfaces.clear();
#endif
    slot_last_access.clear();
    next_slot = 0;
}

} // namespace chronon3d::backends::vulkan
