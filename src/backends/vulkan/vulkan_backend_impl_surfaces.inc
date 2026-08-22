// Vulkan Impl surface ownership and upload helpers. Included inside VulkanBackend::Impl to keep
// the private state definition single-source while separating responsibilities.

    // Pre-allocate every physical surface from the compiler's interval-coloring
    // plan.  After this call, ensure_surface() performs direct slot lookup
    // instead of search/create/resize—zero vkCreateImage, zero vkCreateImageView,
    // zero vkAllocateMemory in the frame hot loop.
    //
    // The pool-based fallback (ensure_surface without plan) remains for:
    //   - software/reference backends
    //   - job-persistent assets (decoded images, text atlases)
    //   - surfaces created outside the compiled graph path
    void preallocate_plan_surfaces(
        std::uint32_t canvas_width,
        std::uint32_t canvas_height,
        const graph::PhysicalFramebufferAllocationPlan& plan) {

        if (canvas_width == 0 || canvas_height == 0 || plan.empty()) return;

        // Resize the plan's slots with actual canvas dimensions.  The
        // compiled plan is resolution-independent; max_width/max_height
        // start at 0 and must be filled with real frame sizes.
        std::vector<graph::FramebufferSlot> resolved_slots = plan.slots;
        const auto frame_bytes = static_cast<std::size_t>(canvas_width) *
            canvas_height * sizeof(float) * 4;  // RGBA32F

        for (auto& slot : resolved_slots) {
            if (slot.max_width < canvas_width) slot.max_width = canvas_width;
            if (slot.max_height < canvas_height) slot.max_height = canvas_height;
        }

        // Pre-create every physical slot as a device-local RGBA32F image.
        // The VulkanMemoryArena batches allocations, so even for many
        // slots the actual vkAllocateMemory count stays low (arena-allocated
        // chunks).
        for (const auto& slot : resolved_slots) {
            const std::uint32_t w = slot.max_width > 0
                ? static_cast<std::uint32_t>(slot.max_width)
                : canvas_width;
            const std::uint32_t h = slot.max_height > 0
                ? static_cast<std::uint32_t>(slot.max_height)
                : canvas_height;

            if (w == 0 || h == 0) continue;

            auto& physical = physical_surfaces[slot.id];
            if (physical.image.image != VK_NULL_HANDLE) {
                destroy_image(physical.image);
            }
            make_image(physical.image, w, h, /*exportable=*/false);
            physical.image.initialized = false;
            physical.desc = runtime::SurfaceDesc{
                w, h,
                runtime::PixelFormat::Rgba32Float,
                runtime::ResourceUsage::Storage,
                runtime::LifetimeClass::FrameTransient,
                frame_bytes};
            ++stats.surface_creations;
        }

        // Remember the plan dimensions so ensure_surface can validate
        // without searching.
        plan_preallocated = true;
        plan_canvas_width = canvas_width;
        plan_canvas_height = canvas_height;
    }

    // When the plan is preallocated, ensure_surface is a direct binding
    // operation: bind a logical handle to its planned physical slot with
    // zero search, zero create, zero resize.  If the handle has no planned
    // slot, it falls through to the pool path (job-persistent assets,
    // unplanned scratch surfaces).
    bool plan_preallocated{false};
    std::uint32_t plan_canvas_width{0};
    std::uint32_t plan_canvas_height{0};

    static bool surface_compatible(const runtime::SurfaceDesc& a,
                                   const runtime::SurfaceDesc& b) {
        return a.width == b.width && a.height == b.height && a.format == b.format;
    }

    // True when at least one logical handle currently references `slot`.
    bool slot_in_use(std::size_t slot) const {
        for (const auto& [handle, bound_slot] : surface_bindings) {
            (void)handle;
            if (bound_slot == slot) return true;
        }
        return false;
    }

    std::size_t bound_slot(runtime::RenderSurfaceHandle handle) const {
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

    // The single resolve path: handle → slot → backing image.
    Image& resolve_image(runtime::RenderSurfaceHandle handle) {
        const auto slot = bound_slot(handle);
        const auto physical_it = physical_surfaces.find(slot);
        if (physical_it == physical_surfaces.end()) {
            throw std::invalid_argument("Vulkan physical slot has no backing image");
        }
        return physical_it->second.image;
    }

    // True when some handle OTHER than `self` is bound to `slot` and the
    // slot's image already holds content.  Pre-initialized images (uploaded
    // assets) are never aliased: the liveness model covers in-frame
    // producers only, so sharing such a slot with a writer could clobber
    // pixels the frame still needs to sample.
    bool slot_has_initialized_occupant(std::size_t slot,
                                       runtime::RenderSurfaceHandle self) const {
        const auto physical_it = physical_surfaces.find(slot);
        if (physical_it == physical_surfaces.end() ||
            !physical_it->second.image.initialized) {
            return false;
        }
        for (const auto& [handle, bound_slot] : surface_bindings) {
            if (handle != self && bound_slot == slot) return true;
        }
        return false;
    }

    // Bind a handle to a slot, creating (or resizing) the slot's single
    // backing image as needed.  Aliased handles resolve to the same image
    // (one VkImage per slot, never per handle).  Two conservative guards
    // keep aliasing safe when content already exists:
    //   * a handle whose image holds content (uploaded before the batch) is
    //     PINNED to its current slot — pixels never migrate to another slot;
    //   * a handle never aliases a slot whose occupant image is initialized
    //     — such a slot is DIVERTED to a fresh private slot instead.
    Image& bind_handle_to_slot(runtime::RenderSurfaceHandle handle,
                               std::size_t slot,
                               const runtime::SurfaceDesc& desc) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_slot = previous->second;
            const auto old_it = physical_surfaces.find(old_slot);
            const bool pinned = old_it != physical_surfaces.end() &&
                                old_it->second.image.initialized;
            if (pinned) {
                slot = old_slot;  // content stays where it is
            } else {
                surface_bindings.erase(previous);
            }
        }
        if (slot_has_initialized_occupant(slot, handle)) {
            slot = next_slot++;  // never share pre-initialized content
        }
        auto& physical = physical_surfaces[slot];
        stats.physical_surfaces_peak = std::max(
            stats.physical_surfaces_peak,
            static_cast<std::uint64_t>(physical_surfaces.size()));
        // When the plan is preallocated, the image already exists with the
        // correct dimensions — no create, no resize.  Validate the invariant
        // and skip all Vulkan object creation.
        if (plan_preallocated && physical.image.image != VK_NULL_HANDLE) {
            if (physical.image.width != desc.width ||
                physical.image.height != desc.height) {
                throw std::invalid_argument(
                    "Vulkan plan-preallocated surface slot " +
                    std::to_string(slot) + " has dimensions " +
                    std::to_string(physical.image.width) + "x" +
                    std::to_string(physical.image.height) +
                    " but requested " + std::to_string(desc.width) + "x" +
                    std::to_string(desc.height));
            }
            physical.desc = desc;
            surface_bindings[handle] = slot;
            return physical.image;
        }
        if (physical.image.image == VK_NULL_HANDLE ||
            physical.image.width != desc.width ||
            physical.image.height != desc.height) {
            if (physical.image.image != VK_NULL_HANDLE) destroy_image(physical.image);
            make_image(physical.image, desc.width, desc.height, false,
                       to_vk_format(desc.format));
            physical.image.initialized = false;
        }
        physical.desc = desc;
        surface_bindings[handle] = slot;
        return physical.image;
    }

    static VkFormat to_vk_format(runtime::PixelFormat fmt) noexcept {
        switch (fmt) {
            case runtime::PixelFormat::Rgba32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case runtime::PixelFormat::Rgba8Unorm:  return VK_FORMAT_B8G8R8A8_UNORM;
            case runtime::PixelFormat::R8Unorm:     return VK_FORMAT_R8_UNORM;
            default: return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    }

    static constexpr std::size_t pixel_format_bytes(runtime::PixelFormat fmt) noexcept {
        switch (fmt) {
            case runtime::PixelFormat::Rgba32Float: return 16;
            case runtime::PixelFormat::Rgba8Unorm:  return 4;
            case runtime::PixelFormat::R8Unorm:     return 1;
            default: return 16;
        }
    }

    Image& ensure_surface(runtime::RenderSurfaceHandle handle,
                          const runtime::SurfaceDesc& desc) {
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            (desc.format != runtime::PixelFormat::Rgba32Float &&
             desc.format != runtime::PixelFormat::Rgba8Unorm &&
             desc.format != runtime::PixelFormat::R8Unorm) ||
            desc.width == 0 || desc.height == 0) {
            throw std::invalid_argument("Vulkan surface requires a valid non-empty description");
        }
        const auto binding = surface_bindings.find(handle);
        if (binding != surface_bindings.end()) {
            auto& physical = physical_surfaces.at(binding->second);
            if (physical.image.width != desc.width ||
                physical.image.height != desc.height) {
                destroy_image(physical.image);
                make_image(physical.image, desc.width, desc.height);
                physical.desc = desc;
            }
            ensure_descriptor_set();
            return physical.image;
        }
        // Job-persistent assets (decoded images, packed text atlases, etc.)
        // must never alias a transient graph slot.  Their logical handle
        // survives frame boundaries and is sampled by later jobs; reusing a
        // free-looking physical slot here can race with deferred releases or
        // let a later transient writer mutate the cached asset.  Allocate a
        // dedicated physical image for the whole job/daemon lifetime.
        if (desc.lifetime != runtime::LifetimeClass::JobPersistent) {
            // Unbound transient: alias an exact-match compatible unused physical slot first
            for (auto& [slot, physical] : physical_surfaces) {
                if (!slot_in_use(slot) && surface_compatible(physical.desc, desc)) {
                    ensure_descriptor_set();
                    return bind_handle_to_slot(handle, slot, desc);
                }
            }
            // Next, reuse any unused physical slot (it will be resized by bind_handle_to_slot)
            for (auto& [slot, physical] : physical_surfaces) {
                if (!slot_in_use(slot) && physical.desc.lifetime != runtime::LifetimeClass::JobPersistent) {
                    ensure_descriptor_set();
                    return bind_handle_to_slot(handle, slot, desc);
                }
            }
        }
        ensure_descriptor_set();
        return bind_handle_to_slot(handle, next_slot++, desc);
    }

    void wait_upload_slot(UploadSlot& slot) {
        if (!slot.in_flight) return;
        check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences(upload slot)");
        check(vkResetFences(device, 1, &slot.fence), "vkResetFences(upload slot)");
        slot.in_flight = false;
    }

    UploadSlot& acquire_upload_slot(bool wait_for_completion) {
        // Synchronous callers retain the warmed first slot.  Asynchronous
        // callers rotate through the ring and only wait when all slots are
        // occupied, allowing several decoded assets to be queued together.
        if (wait_for_completion) {
            wait_upload_slot(upload_slots[0]);
            return upload_slots[0];
        }
        for (std::size_t attempt = 0; attempt < kUploadSlotCount; ++attempt) {
            auto& slot = upload_slots[next_upload_slot];
            next_upload_slot = (next_upload_slot + 1) % kUploadSlotCount;
            if (!slot.in_flight) return slot;
            if (vkGetFenceStatus(device, slot.fence) == VK_SUCCESS) {
                wait_upload_slot(slot);
                return slot;
            }
        }
        auto& oldest = upload_slots[next_upload_slot];
        wait_upload_slot(oldest);
        next_upload_slot = (next_upload_slot + 1) % kUploadSlotCount;
        return oldest;
    }

    void ensure_upload_slot(UploadSlot& slot, VkDeviceSize bytes) {
        wait_upload_slot(slot);
        if (slot.capacity < bytes) {
            if (slot.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, slot.buffer, nullptr);
            if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(device, slot.memory, nullptr);
            const VkBufferCreateInfo info{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
            check(vkCreateBuffer(device, &info, nullptr, &slot.buffer),
                  "vkCreateBuffer(upload slot)");
            ++stats.staging_allocations;
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, slot.buffer, &requirements);
            const VkMemoryAllocateInfo allocation{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
                memory_type(requirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
            check(vkAllocateMemory(device, &allocation, nullptr, &slot.memory),
                  "vkAllocateMemory(upload slot)");
            check(vkBindBufferMemory(device, slot.buffer, slot.memory, 0),
                  "vkBindBufferMemory(upload slot)");
            slot.capacity = bytes;
        }
        if (slot.command_buffer == VK_NULL_HANDLE) {
            const VkCommandBufferAllocateInfo command_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
            check(vkAllocateCommandBuffers(device, &command_info, &slot.command_buffer),
                  "vkAllocateCommandBuffers(upload slot)");
            const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            check(vkCreateFence(device, &fence_info, nullptr, &slot.fence),
                  "vkCreateFence(upload slot)");
        }
    }

    void release_surface_now(runtime::RenderSurfaceHandle handle) {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return;
        wait_for_pending();
        const auto slot = binding->second;
        surface_bindings.erase(binding);
        // Retain physical_surfaces[slot] so subsequent ensure_surface calls
        // can reuse the allocated physical surface without re-allocating memory!
        unplanned_surface_handles.erase(handle);
        ++stats.surface_releases;
    }

    void flush_deferred_surface_releases() {
        if (deferred_surface_releases.empty()) return;
        auto pending = std::move(deferred_surface_releases);
        deferred_surface_releases.clear();
        for (const auto handle : pending) release_surface_now(handle);
    }

    void release_frame_transient_surfaces() noexcept {
        try {
            // This is invoked only after a complete job. Drain the device
            // once before destroying orphaned images; per-image destruction
            // while submissions are in flight can serialize inside the
            // driver and make daemon recovery appear hung.
            check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(transient cleanup)");
            std::vector<runtime::RenderSurfaceHandle> handles;
            handles.reserve(surface_bindings.size());
            for (const auto& [handle, slot] : surface_bindings) {
                const auto it = physical_surfaces.find(slot);
                if (it != physical_surfaces.end() &&
                    it->second.desc.lifetime == runtime::LifetimeClass::FrameTransient &&
                    !unplanned_surface_handles.contains(handle)) {
                    handles.push_back(handle);
                }
            }
            for (const auto handle : handles) {
                const auto binding = surface_bindings.find(handle);
                if (binding == surface_bindings.end()) continue;
                const auto slot = binding->second;
                surface_bindings.erase(binding);
                unplanned_surface_handles.erase(handle);
                const auto it = physical_surfaces.find(slot);
                if (it != physical_surfaces.end()) {
                    it->second.image.initialized = false;
                }
                ++stats.surface_releases;
            }
            if (physical_surfaces.size() > 32) {
                spdlog::warn("[vulkan] transient cleanup retained bindings={} physical_surfaces={} unplanned={}",
                             surface_bindings.size(), physical_surfaces.size(),
                             unplanned_surface_handles.size());
            }
        } catch (const std::exception& error) {
            spdlog::error("[vulkan] transient cleanup failed: {}", error.what());
        } catch (...) {
            // Cleanup is best-effort and must not terminate the daemon.
        }
    }

    void retire_completed_frame_transient_surfaces() noexcept {
        try {
            // This is deliberately a status query, never a wait.  A transient
            // image is eligible for destruction only after every Vulkan
            // submission that could reference it has completed.  The normal
            // frame path calls this opportunistically; the final cleanup path
            // above remains the authoritative blocking drain.
            if (pending_timeline_value != 0 &&
                vkGetFenceStatus(device, fence) != VK_SUCCESS) {
                return;
            }
            for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
                if (frame_batch.in_flight[i] &&
                    vkGetFenceStatus(device, frame_batch.fences[i]) != VK_SUCCESS) {
                    return;
                }
            }
            for (const auto& upload : upload_slots) {
                if (upload.in_flight &&
                    vkGetFenceStatus(device, upload.fence) != VK_SUCCESS) {
                    return;
                }
            }

            std::vector<runtime::RenderSurfaceHandle> handles;
            handles.reserve(surface_bindings.size());
            for (const auto& [handle, slot] : surface_bindings) {
                const auto it = physical_surfaces.find(slot);
                if (it != physical_surfaces.end() &&
                    it->second.desc.lifetime == runtime::LifetimeClass::FrameTransient &&
                    !unplanned_surface_handles.contains(handle)) {
                    handles.push_back(handle);
                }
            }
            for (const auto handle : handles) {
                const auto binding = surface_bindings.find(handle);
                if (binding == surface_bindings.end()) continue;
                const auto slot = binding->second;
                surface_bindings.erase(binding);
                unplanned_surface_handles.erase(handle);
                const auto it = physical_surfaces.find(slot);
                if (it != physical_surfaces.end()) {
                    it->second.image.initialized = false;
                }
                ++stats.surface_releases;
            }
        } catch (const std::exception& error) {
            spdlog::error("[vulkan] non-blocking transient retirement failed: {}", error.what());
        } catch (...) {
            // Best effort: final job cleanup remains responsible for recovery.
        }
    }

    std::uint64_t submit_upload(UploadSlot& slot, bool wait_for_completion) {
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

    std::uint64_t upload(runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
                         std::span<const float> rgba, bool wait_for_completion) {
        auto& image = ensure_surface(handle, desc);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(desc.width) * desc.height * sizeof(float) * 4;
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan upload size does not match surface");
        ++stats.upload_calls;
        stats.upload_bytes += bytes;
        stats.upload_full_surface_bytes += bytes;
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += bytes;
            stats.upload_producer_full_count[producer] += 1;
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, bytes);
        void* mapped = nullptr;
        check(vkMapMemory(device, slot.memory, 0, bytes, 0, &mapped), "vkMapMemory(surface upload)");
        std::memcpy(mapped, rgba.data(), static_cast<std::size_t>(bytes));
        vkUnmapMemory(device, slot.memory);
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
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        const auto ticket = submit_upload(slot, wait_for_completion);
        return ticket;
    }

    std::uint64_t upload_region(runtime::RenderSurfaceHandle handle,
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
        void* mapped = nullptr;
        check(vkMapMemory(device, slot.memory, 0, byte_count, 0, &mapped), "vkMapMemory(surface region upload)");
        if (desc.format == runtime::PixelFormat::R8Unorm) {
            // Pack float→R8: clamp to [0,1], convert to uint8
            auto* dst = static_cast<std::uint8_t*>(mapped);
            for (std::size_t i = 0; i < rgba.size(); ++i) {
                dst[i] = static_cast<std::uint8_t>(
                    std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        } else {
            std::memcpy(mapped, rgba.data(), static_cast<std::size_t>(byte_count));
        }
        vkUnmapMemory(device, slot.memory);
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
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }

    void wait_upload_ticket(std::uint64_t ticket) {
        const VkSemaphoreWaitInfo wait_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1,
            &timeline_semaphore, &ticket};
        check(vkWaitSemaphores(device, &wait_info, UINT64_MAX),
              "vkWaitSemaphores(upload ticket)");
        for (auto& slot : upload_slots) {
            if (slot.in_flight && slot.ticket == ticket) wait_upload_slot(slot);
        }
    }

    void download(runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
        if (surface_bindings.count(handle) == 0) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        auto& image = resolve_image(handle);
        if (!image.initialized) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(image.width) * image.height * sizeof(float) * 4;
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
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        submit();
        const auto readback_start = profiling::now();
        void* mapped = nullptr;
        check(vkMapMemory(device, staging_memory, 0, bytes, 0, &mapped), "vkMapMemory(surface download)");
        std::memcpy(rgba.data(), mapped, static_cast<std::size_t>(bytes));
        vkUnmapMemory(device, staging_memory);
        stats.readback_us += static_cast<std::uint64_t>(profiling::elapsed_us(readback_start));
    }
