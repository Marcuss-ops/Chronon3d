// Vulkan Impl resource, descriptor, and barrier helpers. Included inside VulkanBackend::Impl to keep
// the private state definition single-source while separating responsibilities.

    void destroy_image(Image& target) {
        if (target.cuda_to_vulkan != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, target.cuda_to_vulkan, nullptr);
        }
        if (target.vulkan_to_cuda != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, target.vulkan_to_cuda, nullptr);
        }
        if (target.view != VK_NULL_HANDLE) vkDestroyImageView(device, target.view, nullptr);
        if (target.image != VK_NULL_HANDLE) {
            VulkanImageAllocation img_alloc{
                .image = target.image,
                .allocation = target.allocation,
                .size = 0,
                .exportable = target.exportable
            };
            memory_manager.destroy_image(img_alloc);
        }
        target = {};
    }

    void destroy_upload_slot(VulkanUploadRing::UploadSlot& slot) {
        if (slot.command_buffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
        }
        if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(device, slot.fence, nullptr);
        if (slot.buffer_allocation.buffer != VK_NULL_HANDLE) {
            memory_manager.destroy_buffer(slot.buffer_allocation);
        }
        slot = {};
    }

    void make_image(Image& target, std::uint32_t width, std::uint32_t height,
                    bool exportable = false,
                    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT) {
        VkExternalMemoryImageCreateInfo external_image{
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external_image.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkImageCreateInfo info{
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D,
            format, {width, height, 1}, 1, 1,
            VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
        info.pNext = exportable ? &external_image : nullptr;

        const auto mem_class = exportable
            ? VulkanMemoryClass::ExternalExportable
            : VulkanMemoryClass::DeviceLocal;
        const auto alloc = memory_manager.create_image(info, mem_class);
        target.image = alloc.image;
        target.allocation = alloc.allocation;
        if (debug_context) {
            debug_context->set_image_name(target.image, exportable ? "Chronon3D.CudaExportableImage" : "Chronon3D.DeviceLocalImage");
        }

        const VkImageViewCreateInfo view{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, target.image,
            VK_IMAGE_VIEW_TYPE_2D, format, {},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        check(vkCreateImageView(device, &view, nullptr, &target.view), "vkCreateImageView");
        if (debug_context) {
            debug_context->set_image_view_name(target.view, exportable ? "Chronon3D.CudaExportableImageView" : "Chronon3D.DeviceLocalImageView");
        }
        target.width = width;
        target.height = height;
        target.format = format;
        target.exportable = exportable;
    }

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    VkSemaphore make_external_binary_semaphore() {
        VkExportSemaphoreCreateInfo export_info{
            VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
        export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        const VkSemaphoreCreateInfo info{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &export_info, 0};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        check(vkCreateSemaphore(device, &info, nullptr, &semaphore),
              "vkCreateSemaphore(external CUDA interop)");
        if (debug_context) {
            debug_context->set_semaphore_name(semaphore, "Chronon3D.Semaphore.CudaInterop");
        }
        return semaphore;
    }

    void create_cuda_external_surface(runtime::RenderSurfaceHandle handle,
                                      const runtime::SurfaceDesc& desc) {
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            desc.width == 0 || desc.height == 0 ||
            (desc.format != runtime::PixelFormat::Rgba32Float &&
             desc.format != runtime::PixelFormat::Rgba8Unorm &&
             desc.format != runtime::PixelFormat::Nv12 &&
             desc.format != runtime::PixelFormat::P010)) {
            throw std::invalid_argument(
                "CUDA external surface requires non-empty supported description");
        }
        if (surfaces.surface_bindings.contains(handle)) {
            throw std::invalid_argument("CUDA external surface handle already exists");
        }
        const auto slot = surfaces.next_slot++;
        auto& physical = surfaces.physical_surfaces[slot];
        make_image(physical.image, desc.width, desc.height, true,
                   to_vk_format(desc.format));
        physical.image.cuda_to_vulkan = make_external_binary_semaphore();
        physical.image.vulkan_to_cuda = make_external_binary_semaphore();
        physical.desc = desc;
        surfaces.surface_bindings.emplace(handle, slot);
        // Encode surfaces are created outside the render graph and are fully
        // consumed by copy_surface_to_cuda_encoder before their release.
        // Mark them unplanned so the writer can reclaim them immediately even
        // while the render thread records the next frame batch.
        surfaces.unplanned_surface_handles.insert(handle);
        ++stats.surface_creations;
    }

    CudaExternalMemoryInfo export_cuda_external_memory(
        runtime::RenderSurfaceHandle handle) const {
        const auto binding = surfaces.surface_bindings.find(handle);
        if (binding == surfaces.surface_bindings.end()) {
            throw std::invalid_argument("CUDA external surface handle is not bound");
        }
        const auto image_it = surfaces.physical_surfaces.find(binding->second);
        if (image_it == surfaces.physical_surfaces.end() ||
            !image_it->second.image.exportable) {
            throw std::invalid_argument("surface is not exportable to CUDA");
        }
        const auto alloc_info = memory_manager.allocation_info(image_it->second.image.allocation);
        if (alloc_info.offset != 0) {
            throw std::runtime_error("External Vulkan allocation must be dedicated (offset == 0)");
        }
        auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
            vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
        if (!get_fd) throw std::runtime_error("vkGetMemoryFdKHR is unavailable");
        VkMemoryGetFdInfoKHR info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        info.memory = alloc_info.deviceMemory;
        info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        int fd = -1;
        check(get_fd(device, &info, &fd), "vkGetMemoryFdKHR");
        auto get_semaphore_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
            vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
        if (!get_semaphore_fd) {
            close(fd);
            throw std::runtime_error("vkGetSemaphoreFdKHR is unavailable");
        }
        const auto& image = image_it->second.image;
        VkSemaphoreGetFdInfoKHR cuda_to_vulkan_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr,
            image.cuda_to_vulkan,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
        VkSemaphoreGetFdInfoKHR vulkan_to_cuda_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr,
            image.vulkan_to_cuda,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
        int cuda_to_vulkan_fd = -1;
        int vulkan_to_cuda_fd = -1;
        check(get_semaphore_fd(device, &cuda_to_vulkan_info, &cuda_to_vulkan_fd),
              "vkGetSemaphoreFdKHR(cuda to Vulkan)");
        const auto second_semaphore_result = get_semaphore_fd(
            device, &vulkan_to_cuda_info, &vulkan_to_cuda_fd);
        if (second_semaphore_result != VK_SUCCESS) {
            close(fd);
            close(cuda_to_vulkan_fd);
            check(second_semaphore_result, "vkGetSemaphoreFdKHR(Vulkan to CUDA)");
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image_it->second.image.image,
                                     &requirements);
        return CudaExternalMemoryInfo{
            fd, cuda_to_vulkan_fd, vulkan_to_cuda_fd, requirements.size,
            image_it->second.image.width, image_it->second.image.height,
            image_it->second.image.format == VK_FORMAT_B8G8R8A8_UNORM ? 2u : 1u};
    }

    void prepare_cuda_surface_for_vulkan(runtime::RenderSurfaceHandle handle) {
        const auto slot = bound_slot(handle);
        const auto it = surfaces.physical_surfaces.find(slot);
        if (it == surfaces.physical_surfaces.end() || !it->second.image.exportable ||
            it->second.image.cuda_to_vulkan == VK_NULL_HANDLE ||
            it->second.image.vulkan_to_cuda == VK_NULL_HANDLE) {
            throw std::invalid_argument("surface is not a CUDA external surface");
        }
        // CUDA signals cuda_to_vulkan after writing the imported image. The
        // next Vulkan submit consumes that signal and signals vulkan_to_cuda
        // after its compositing work completes.
        // The CUDA producer has fully populated the image, so subsequent
        // Vulkan operations must transition from GENERAL rather than treat
        // the imported image as undefined on the first composite.
        it->second.image.initialized = true;
        surfaces.cuda_ready_surfaces.insert(slot);
    }

    void copy_surface_to_cuda_encoder(runtime::RenderSurfaceHandle source,
                                      runtime::RenderSurfaceHandle destination,
                                      bool wait_for_completion) {
        const auto source_slot = bound_slot(source);
        const auto destination_slot = bound_slot(destination);
        auto& src = surfaces.physical_surfaces.at(source_slot).image;
        auto& dst = surfaces.physical_surfaces.at(destination_slot).image;
        if (!dst.exportable || dst.format != VK_FORMAT_B8G8R8A8_UNORM) {
            throw std::invalid_argument("CUDA encoder destination must be exportable B8G8R8A8");
        }
        const bool record_in_frame_batch = frame_batch.active;
        if (!record_in_frame_batch) begin_command_buffer();
        const VkCommandBuffer command = record_in_frame_batch
            ? active_command_buffer() : command_buffer;
        transition(command, src.image,
                   src.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition(command, dst.image,
                   dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {static_cast<int>(src.width), static_cast<int>(src.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[1] = {static_cast<int>(dst.width), static_cast<int>(dst.height), 1};
        vkCmdBlitImage(command, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                       VK_FILTER_NEAREST);
        transition(command, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        src.initialized = true;
        dst.initialized = true;
        surfaces.cuda_export_ready_surfaces.insert(destination_slot);
        if (!record_in_frame_batch) submit(wait_for_completion);
    }
#endif

    void ensure_descriptor_set() {
        if (descriptor_set != VK_NULL_HANDLE) return;
        const VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptor_pool, 1, &descriptor_layout};
        check(vkAllocateDescriptorSets(device, &allocation, &descriptor_set), "vkAllocateDescriptorSets");
    }

    void bind_descriptors(const Image& destination, const Image& source) {
        ensure_descriptor_set();
        write_descriptors(descriptor_set, destination, source);
    }

    void write_descriptors(VkDescriptorSet set,
                           const Image& destination, const Image& source) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo src_info{VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr}};
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    // fill_rect has a single-image binding (destination only, no source).
    void write_fill_rect_descriptors(VkDescriptorSet set, const Image& destination) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr}};
        vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
    }

    void bind_fill_rect_descriptors(const Image& destination) {
        ensure_descriptor_set();
        write_fill_rect_descriptors(descriptor_set, destination);
    }

    void write_matte_descriptors(VkDescriptorSet set, const Image& destination,
                                 const Image& target, const Image& matte) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo target_info{VK_NULL_HANDLE, target.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo matte_info{VK_NULL_HANDLE, matte.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &target_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &matte_info, nullptr, nullptr}};
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    void write_text_run_descriptors(VkDescriptorSet set, const Image& destination,
                                    const Image& atlas, VkBuffer instance_buffer) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo atlas_info{VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    void write_layer_batch_descriptors(VkDescriptorSet set, const Image& destination,
                                       const Image& source, VkBuffer instance_buffer) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo src_info{VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    void write_text_batch_descriptors(VkDescriptorSet set, const Image& destination,
                                      const Image& atlas, VkBuffer glyph_buffer,
                                      VkBuffer run_buffer) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo atlas_info{VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorBufferInfo glyph_info{glyph_buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo run_info{run_buffer, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph_info, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &run_info, nullptr}};
        vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    }

    VkDescriptorSet ensure_glow_descriptor_set(std::size_t index) {
        auto& set = glow_descriptor_sets[index];
        if (set != VK_NULL_HANDLE) return set;
        const VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
            descriptor_pool, 1, &descriptor_layout};
        check(vkAllocateDescriptorSets(device, &allocation, &set),
              "vkAllocateDescriptorSets(glow)");
        return set;
    }

    // The command buffer of the slot currently being recorded.  next_slot is
    // stable during a batch; it only advances in submit_batch(), so this is
    // the correct target for every record_* call of the active frame.
    [[nodiscard]] VkCommandBuffer active_command_buffer() const noexcept {
        return frame_batch.command_buffers[frame_batch.next_slot];
    }

    // Allocate a descriptor set for one recorded pass of the active frame
    // batch from the CURRENT slot's allocator.  Each pass binds its own set
    // so the image bindings written now stay valid until end_frame_batch()
    // submits the whole batch; the sets are tracked so begin_frame_batch()
    // invalidates them with the slot's next allocator reset.
    VkDescriptorSet allocate_pass_descriptor_set() {
        const auto set =
            frame_batch.descriptor_allocators[frame_batch.next_slot].allocate();
        frame_batch.descriptor_sets.push_back(set);
        ++stats.descriptor_allocations;
        return set;
    }

    VkDescriptorSet allocate_text_tile_bin_descriptor_set() {
        if (!frame_batch.active) {
            throw std::logic_error("text tile bin requires an active frame batch");
        }
        return frame_batch.text_tile_bin_allocators[frame_batch.next_slot].allocate();
    }

    VkDescriptorSet allocate_text_tile_raster_descriptor_set() {
        if (!frame_batch.active) {
            throw std::logic_error("text tile raster requires an active frame batch");
        }
        return frame_batch.text_tile_raster_allocators[frame_batch.next_slot].allocate();
    }

    void write_text_tile_bin_descriptors(
        VkDescriptorSet set, VkBuffer glyph_buffer, VkBuffer run_buffer,
        VkBuffer tile_counts, VkBuffer tile_indices) {
        const VkDescriptorBufferInfo glyph{glyph_buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo runs{run_buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo counts{tile_counts, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo indices{tile_indices, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &runs, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &counts, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indices, nullptr}};
        vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    }

    void write_text_tile_raster_descriptors(
        VkDescriptorSet set, const Image& destination, const Image& atlas,
        VkBuffer glyph_buffer, VkBuffer run_buffer,
        VkBuffer tile_counts, VkBuffer tile_indices) {
        const VkDescriptorImageInfo dst{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo atlas_info{VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorBufferInfo glyph{glyph_buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo runs{run_buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo counts{tile_counts, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo indices{tile_indices, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &runs, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &counts, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indices, nullptr}};
        vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
    }

    // ── synchronization helpers (single emission site) ───────────────────────
    // Layout transitions and memory barriers for the pass pipeline are built
    // ONLY here.  Kernels (record_*) never synchronize; the operation
    // wrappers route through either the BarrierPlan mapper (plan-driven
    // batches via begin_plan_batch) or the conservative fallback (standalone
    // ops and direct op calls without a plan).

    VkImageMemoryBarrier make_image_barrier(const Image& image,
                                            VkImageLayout old_layout,
                                            VkImageLayout new_layout,
                                            VkAccessFlags src_access,
                                            VkAccessFlags dst_access) const {
        return VkImageMemoryBarrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
            src_access, dst_access, old_layout, new_layout,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image.image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    }

    void emit_barriers(VkCommandBuffer command, VkPipelineStageFlags src_stage,
                       VkPipelineStageFlags dst_stage,
                       const std::vector<VkImageMemoryBarrier>& barriers) {
        if (barriers.empty()) return;
        stats.barriers_emitted += barriers.size();
        vkCmdPipelineBarrier(command, src_stage, dst_stage, 0,
                             0, nullptr, 0, nullptr,
                             static_cast<std::uint32_t>(barriers.size()),
                             barriers.data());
    }

    // Conservative fallback for plan-less paths (standalone ops and direct
    // op calls inside a plain begin_frame_batch): one full image memory
    // barrier per accessed surface plus the first-write layout transition.
    // This reproduces the legacy per-pass synchronization exactly, but lives
    // in ONE place instead of being duplicated inside every kernel.
    void emit_conservative_pass_sync(VkCommandBuffer command,
                                     std::initializer_list<const Image*> images) {
        std::vector<VkImageMemoryBarrier> barriers;
        barriers.reserve(images.size());
        for (const Image* image : images) {
            barriers.push_back(make_image_barrier(
                *image,
                image->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
        }
        emit_barriers(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, barriers);
    }

    // Route one pass's synchronization: the BarrierPlan mapper when the
    // batch is plan-driven, the conservative fallback otherwise.
    void emit_pass_sync(VkCommandBuffer command,
                        std::initializer_list<const Image*> images) {
        if (frame_batch.sync_plan) {
            emit_plan_pass_barriers(command, *frame_batch.sync_plan,
                                    frame_batch.pass_count);
            for (const Image* image : images) {
                bool unplanned = false;
                for (const auto handle : surfaces.unplanned_surface_handles) {
                    const auto binding = surfaces.surface_bindings.find(handle);
                    if (binding == surfaces.surface_bindings.end()) continue;
                    const auto physical = surfaces.physical_surfaces.find(binding->second);
                    if (physical != surfaces.physical_surfaces.end() &&
                        &physical->second.image == image) {
                        unplanned = true;
                        break;
                    }
                }
                if (unplanned) {
                    emit_conservative_pass_sync(command, images);
                    break;
                }
            }
        } else {
            emit_conservative_pass_sync(command, images);
        }
    }

    // Cross-overlay boundary inside a command batch.  When the next overlay
    // begins, every physical image the previous overlay wrote must be visible
    // before any of the next overlay's passes sample it, regardless of how
    // logical handles alias slots across the two overlays.  A single
    // conservative full-barrier over all initialized images is the safe,
    // per-boundary cost (once per overlay, not per pass).
    void emit_command_batch_boundary() {
        std::vector<VkImageMemoryBarrier> barriers;
        for (auto& [slot, physical] : surfaces.physical_surfaces) {
            (void)slot;
            if (!physical.image.initialized) continue;
            barriers.push_back(make_image_barrier(
                physical.image,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
        }
        emit_barriers(active_command_buffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, barriers);
    }

    // The single BarrierPlan→Vulkan mapper.  Emits, into `command`, the
    // image memory barriers the plan requires before the pass at
    // `pass_index` records its dispatch:
    //   * an in-frame previous access becomes a compute-stage memory barrier
    //     (SHADER_WRITE / SHADER_READ source access from the previous
    //     transition, destination access from the current one) — this covers
    //     the write→read / read→write / write→write chains between passes;
    //   * a first write to a never-initialized surface becomes an
    //     UNDEFINED→GENERAL layout transition (contents discarded);
    //   * a first read needs no barrier: every previous submission (uploads,
    //     earlier batches) is queued before this one on the same queue, so
    //     FIFO ordering already made it visible.
    void emit_plan_pass_barriers(VkCommandBuffer command,
                                 const runtime::BarrierPlan& plan,
                                 std::size_t pass_index) {
        std::vector<VkImageMemoryBarrier> barriers;
        for (const auto& transition : plan.transitions) {
            if (transition.pass_index != pass_index) continue;
            if (transition.surface == runtime::kInvalidRenderSurfaceHandle) continue;
            const auto binding = surfaces.surface_bindings.find(transition.surface);
            if (binding == surfaces.surface_bindings.end()) continue;
            const auto slot = binding->second;
            const auto physical_it = surfaces.physical_surfaces.find(slot);
            if (physical_it == surfaces.physical_surfaces.end()) continue;
            const auto& image = physical_it->second.image;
            const bool is_write =
                transition.access == runtime::ResourceAccess::Write ||
                transition.access == runtime::ResourceAccess::ReadWrite;
            const auto prev_it = surfaces.slot_last_access.find(slot);
            if (prev_it != surfaces.slot_last_access.end()) {
                const bool prev_write =
                    prev_it->second == runtime::ResourceAccess::Write ||
                    prev_it->second == runtime::ResourceAccess::ReadWrite;
                // Read-after-read has no memory dependency.  In particular,
                // a terminal native-video hold surface is sampled by several
                // consecutive frames without being modified between them.
                // Emitting a full image barrier for every READ -> READ edge
                // needlessly serializes command recording and can turn the
                // tail of a held clip into a CPU fence stall.
                if (prev_write || is_write) {
                    barriers.push_back(make_image_barrier(
                        image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        prev_write ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT,
                        is_write ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT));
                }
            } else if (is_write && !image.initialized) {
                barriers.push_back(make_image_barrier(
                    image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT));
            }
            surfaces.slot_last_access[slot] = transition.access;
        }
        emit_barriers(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, barriers);
    }

    // ── record-only kernel primitives ────────────────────────────────────────
    // These functions only append Vulkan commands to the given command
    // buffer: pipeline bind, descriptor bind, push constants, dispatch.
    // They NEVER submit and they NEVER synchronize — layout transitions and
    // memory barriers are emitted by the caller through the single sync
    // helpers below (the BarrierPlan mapper, or the conservative fallback
    // for plan-less calls).  They also never mutate surface state (the
    // `initialized` flags are updated by the operation wrappers).
