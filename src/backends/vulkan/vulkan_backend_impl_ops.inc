// Vulkan Impl surface operation implementations. Included inside VulkanBackend::Impl to keep
// the private state definition single-source while separating responsibilities.
    void composite(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source, BlendMode mode,
                   const std::optional<raster::BBox>& clip = std::nullopt,
                   bool replace = false) {
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width != src_image.width ||
            dst_image.height != src_image.height) {
            throw std::invalid_argument(
                "Vulkan composite references incompatible surfaces: dst=" +
                std::to_string(dst_image.width) + "x" + std::to_string(dst_image.height) +
                " src=" + std::to_string(src_image.width) + "x" +
                std::to_string(src_image.height) +
                (src_image.initialized ? "" : " src-uninitialized"));
        }
        const std::int32_t blend_mode = replace ? 2 : (mode == BlendMode::Add ? 1 : 0);
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_composite(cmd, descriptors, dst_image, src_image,
                             blend_mode, 1.0f, kIdentityTint, clip);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_composite(command_buffer, descriptor_set, dst_image, src_image,
                         blend_mode, 1.0f, kIdentityTint, clip);
        dst_image.initialized = true;
        submit();
    }

    void fill_rect(runtime::RenderSurfaceHandle destination,
                   std::int32_t x0, std::int32_t y0,
                   std::int32_t x1, std::int32_t y1,
                   const Color& color) {
        auto& dst_image = resolve_image(destination);
        if (dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan fill_rect references an empty surface");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_fill_rect_descriptors(descriptors, dst_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image});
            record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1, color);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_fill_rect_descriptors(dst_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image});
        record_fill_rect(command_buffer, descriptor_set, dst_image, x0, y0, x1, y1, color);
        dst_image.initialized = true;
        submit();
    }

    void transform(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source,
                   int offset_x, int offset_y, float opacity) {
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan transform references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_transform(cmd, descriptors,
                             dst_image, src_image, offset_x, offset_y, opacity);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_transform(command_buffer, descriptor_set, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        submit();
    }

    void transform_affine(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle source,
                          const runtime::SurfaceAffineTransform& transform) {
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan affine transform references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_transform_affine(cmd, descriptors,
                                    dst_image, src_image, transform);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_transform_affine(command_buffer, descriptor_set,
                                dst_image, src_image, transform);
        dst_image.initialized = true;
        submit();
    }

    void blur(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source, float radius, bool horizontal) {
        if (!(radius >= 0.0f) || radius > 32.0f) {
            throw std::invalid_argument("Vulkan blur radius must be within [0, 32]");
        }
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width != src_image.width ||
            dst_image.height != src_image.height) {
            throw std::invalid_argument("Vulkan blur references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_blur(cmd, descriptors, dst_image, src_image, radius, horizontal);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_blur(command_buffer, descriptor_set,
                    dst_image, src_image, radius, horizontal);
        dst_image.initialized = true;
        submit();
    }

    void glow(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source,
              runtime::RenderSurfaceHandle scratch_horizontal,
              runtime::RenderSurfaceHandle scratch_vertical,
              float radius, float intensity, const Color& tint) {
        if (!(radius >= 0.0f) || radius > 32.0f) {
            throw std::invalid_argument("Vulkan glow radius must be within [0, 32]");
        }
        auto find_surface = [&](runtime::RenderSurfaceHandle handle) -> Image& {
            return resolve_image(handle);
        };
        auto& dst = find_surface(destination);
        auto& src = find_surface(source);
        auto& horizontal = find_surface(scratch_horizontal);
        auto& vertical = find_surface(scratch_vertical);
        if (!src.initialized || !dst.initialized ||
            src.width != dst.width || src.height != dst.height ||
            horizontal.width != src.width || horizontal.height != src.height ||
            vertical.width != src.width || vertical.height != src.height) {
            throw std::invalid_argument("Vulkan glow surfaces have incompatible dimensions");
        }
        const float tint_rgba[4] = {tint.r, tint.g, tint.b, tint.a};

        if (frame_batch.active) {
            const auto horizontal_descriptor = allocate_pass_descriptor_set();
            write_descriptors(horizontal_descriptor, horizontal, src);
            const auto vertical_descriptor = allocate_pass_descriptor_set();
            write_descriptors(vertical_descriptor, vertical, horizontal);
            const auto composite_descriptor = allocate_pass_descriptor_set();
            write_descriptors(composite_descriptor, dst, vertical);
            // Glow is a single plan pass whose internal blur→blur→composite
            // chain is a write→read dependency chain between dispatches; the
            // full barriers below are glow's own structure (the only
            // multi-dispatch operation), so the pass-level mapper call is
            // skipped for it.
            const auto cmd = active_command_buffer();
            emit_conservative_pass_sync(cmd, {&horizontal, &src});
            record_blur(cmd, horizontal_descriptor, horizontal, src, radius, true);
            emit_conservative_pass_sync(cmd, {&vertical, &horizontal});
            record_blur(cmd, vertical_descriptor, vertical, horizontal, radius, false);
            emit_conservative_pass_sync(cmd, {&dst, &vertical});
            record_composite(cmd, composite_descriptor, dst, vertical,
                             1, intensity, tint_rgba);
            horizontal.initialized = true;
            vertical.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }

        begin_command_buffer();
        const auto horizontal_descriptor = ensure_glow_descriptor_set(0);
        write_descriptors(horizontal_descriptor, horizontal, src);
        const auto vertical_descriptor = ensure_glow_descriptor_set(1);
        write_descriptors(vertical_descriptor, vertical, horizontal);
        const auto composite_descriptor = ensure_glow_descriptor_set(2);
        write_descriptors(composite_descriptor, dst, vertical);
        emit_conservative_pass_sync(command_buffer, {&horizontal, &src});
        record_blur(command_buffer, horizontal_descriptor,
                    horizontal, src, radius, true);
        emit_conservative_pass_sync(command_buffer, {&vertical, &horizontal});
        record_blur(command_buffer, vertical_descriptor,
                    vertical, horizontal, radius, false);
        emit_conservative_pass_sync(command_buffer, {&dst, &vertical});
        record_composite(command_buffer, composite_descriptor,
                         dst, vertical, 1, intensity, tint_rgba);
        horizontal.initialized = true;
        vertical.initialized = true;
        submit();
    }

    void color_adjust(runtime::RenderSurfaceHandle destination,
                      runtime::RenderSurfaceHandle source,
                      float brightness, float contrast,
                      const Color& tint, float tint_amount) {
        auto& dst = resolve_image(destination);
        auto& src = resolve_image(source);
        if (!src.initialized ||
            dst.width != src.width ||
            dst.height != src.height) {
            throw std::invalid_argument("Vulkan color adjust references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst, src);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst, &src});
            record_color_adjust(cmd, descriptors,
                                dst, src, brightness, contrast, tint, tint_amount);
            dst.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst, src);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst, &src});
        record_color_adjust(command_buffer, descriptor_set,
                            dst, src, brightness, contrast, tint, tint_amount);
        dst.initialized = true;
        submit();
    }

    void matte(runtime::RenderSurfaceHandle destination,
               runtime::RenderSurfaceHandle target,
               runtime::RenderSurfaceHandle matte_surface,
               bool luma, bool inverted) {
        auto& dst = resolve_image(destination);
        auto& target_image = resolve_image(target);
        auto& matte_image = resolve_image(matte_surface);
        if (!target_image.initialized || !matte_image.initialized ||
            dst.width != target_image.width ||
            dst.height != target_image.height ||
            dst.width != matte_image.width ||
            dst.height != matte_image.height) {
            throw std::invalid_argument("Vulkan matte references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_matte_descriptors(descriptors, dst, target_image, matte_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst, &target_image, &matte_image});
            record_matte(cmd, descriptors,
                         dst, target_image, matte_image, luma, inverted);
            dst.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        ensure_descriptor_set();
        write_matte_descriptors(descriptor_set, dst, target_image, matte_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst, &target_image, &matte_image});
        record_matte(command_buffer, descriptor_set,
                     dst, target_image, matte_image, luma, inverted);
        dst.initialized = true;
        submit();
    }

    // GPU text-run primitive: composite a batch of glyph quads sampled from a
    // packed atlas texture into the destination in ONE kernel dispatch.
    void text_run_surface(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle atlas,
                          std::span<const runtime::GlyphInstance> glyphs,
                          float current_frame = 0.0f,
                          const Color& highlight_color = Color{},
                          bool highlight_enabled = false) {
        if (glyphs.empty()) {
            throw std::invalid_argument("Vulkan text run requires at least one glyph");
        }
        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(glyphs.size() * sizeof(runtime::GlyphInstance));
        if (bytes > 65536) {
            throw std::invalid_argument(
                "Vulkan text run exceeds the 65536-byte vkCmdUpdateBuffer limit");
        }
        auto& dst_image = resolve_image(destination);
        auto& atlas_image = resolve_image(atlas);
        if (!atlas_image.initialized || !dst_image.initialized ||
            dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan text run references incompatible surfaces");
        }
        // Restrict the compute dispatch to the union of the actual glyph
        // quads. The shader still checks every glyph for correctness, but it
        // no longer launches work for the entire 1080p canvas.
        std::int32_t dispatch_x0 = static_cast<std::int32_t>(dst_image.width);
        std::int32_t dispatch_y0 = static_cast<std::int32_t>(dst_image.height);
        std::int32_t dispatch_x1 = 0;
        std::int32_t dispatch_y1 = 0;
        for (const auto& glyph : glyphs) {
            const auto scaled_w = static_cast<std::int32_t>(std::ceil(
                static_cast<float>(glyph.width) * glyph.scale_x));
            const auto scaled_h = static_cast<std::int32_t>(std::ceil(
                static_cast<float>(glyph.height) * glyph.scale_y));
            dispatch_x0 = std::min(dispatch_x0, glyph.dst_x);
            dispatch_y0 = std::min(dispatch_y0, glyph.dst_y);
            dispatch_x1 = std::max(dispatch_x1, glyph.dst_x + scaled_w);
            dispatch_y1 = std::max(dispatch_y1, glyph.dst_y + scaled_h);
        }
        if (dispatch_x0 >= dispatch_x1 || dispatch_y0 >= dispatch_y1) {
            dispatch_x0 = 0;
            dispatch_y0 = 0;
            dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
            dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
        }
        const std::size_t instance_slot = frame_batch.active
            ? frame_batch.next_slot * kGlyphInstancePassesPerSlot +
                (frame_batch.pass_count % kGlyphInstancePassesPerSlot)
            : 0;
        ensure_glyph_instance_buffer(bytes, instance_slot);
        const VkBuffer instance_buffer = glyph_instance_buffers[instance_slot];
        const std::int32_t glyph_count = static_cast<std::int32_t>(glyphs.size());
        // FNV-1a is sufficient here: this is only a same-process change
        // detector, not an identity/security hash. It lets static text runs
        // reuse the device-local instance buffer across frames.
        std::uint64_t instance_hash = 1469598103934665603ull;
        for (const auto byte : std::as_bytes(glyphs)) {
            instance_hash ^= std::to_integer<std::uint8_t>(byte);
            instance_hash *= 1099511628211ull;
        }
        const bool instance_updated =
            glyph_instance_sizes[instance_slot] != bytes ||
            glyph_instance_hashes[instance_slot] != instance_hash;

        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_text_run_descriptors(descriptors, dst_image, atlas_image, instance_buffer);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &atlas_image});
            if (instance_updated) {
                vkCmdUpdateBuffer(cmd, instance_buffer, 0, bytes, glyphs.data());
                glyph_instance_hashes[instance_slot] = instance_hash;
                glyph_instance_sizes[instance_slot] = bytes;
            }
            record_text_run(cmd, descriptors, dst_image, glyph_count,
                            instance_buffer, instance_updated, current_frame,
                            highlight_color, highlight_enabled,
                            dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        ensure_descriptor_set();
        write_text_run_descriptors(descriptor_set, dst_image, atlas_image, instance_buffer);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &atlas_image});
        if (instance_updated) {
            vkCmdUpdateBuffer(command_buffer, instance_buffer, 0, bytes, glyphs.data());
            glyph_instance_hashes[instance_slot] = instance_hash;
            glyph_instance_sizes[instance_slot] = bytes;
        }
        record_text_run(command_buffer, descriptor_set, dst_image, glyph_count,
                        instance_buffer, instance_updated, current_frame,
                        highlight_color, highlight_enabled,
                        dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
        dst_image.initialized = true;
        ++stats.passes_executed;
        submit();
    }

    // ── execute_layer_batch ──────────────────────────────────────────────
    //
    // GPU layer-batch consumer.  Every LayerInstance in `instances` is
    // composited into `destination` in painter order.  The batch kernel
    // receives:
    //   - instance SSBO (LayerInstance per element)
    //   - resource table (texture/atlas handles indexed by resource_index)
    //   - transform table (4×4 matrices indexed by transform_index)
    //   - paint table (color uniforms indexed by paint_index)
    //
    // For Phase 2 the implementation decomposes into per-instance
    // composite/transform calls — the fused batch kernel lands in a future
    // phase once the IR and fusion analysis are stable.
    void execute_layer_batch(runtime::RenderSurfaceHandle destination,
                             std::span<const runtime::LayerInstance> instances,
                             std::span<const runtime::RenderSurfaceHandle> resources,
                             std::span<const float> transforms,
                             std::span<const float> paints) {
        if (instances.empty()) return;

        auto& dst_image = resolve_image(destination);
        if (!dst_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan layer batch references an invalid destination surface");
        }

        // ── Per-instance dispatch (Phase 2 decomposed path) ────────────
        //
        // Walk instances in painter order.  Each instance resolves its
        // resource handle, computes the destination quad from its dst_*
        // fields, and delegates to the existing transform/composite ops.
        // The `src_x0/y0/x1/y1` fields are used as fractional (0..1) UV
        // coordinates into the source texture.
        //
        // When an instance has a non-trivial transform_index, the
        // transform buffer provides an affine matrix; when paint_index
        // is non-zero, the paint table carries a tint/opacity override.
        //
        // This decomposed path is semantically correct; the fused SSBO
        // batch kernel will replace the per-instance loop in a later
        // phase.
        for (const auto& inst : instances) {
            if (inst.resource_index >= resources.size()) continue;
            const auto source_handle = resources[inst.resource_index];
            if (source_handle == kInvalidRenderSurfaceHandle) continue;

            auto& src_image = resolve_image(source_handle);
            if (!src_image.initialized) continue;

            // Compute destination quad in integer coordinates from the
            // instance's dst_* normalized (0..1) placement.
            const auto dst_w = static_cast<std::int32_t>(
                (inst.dst_x1 - inst.dst_x0) * static_cast<float>(dst_image.width));
            const auto dst_h = static_cast<std::int32_t>(
                (inst.dst_y1 - inst.dst_y0) * static_cast<float>(dst_image.height));
            const auto dst_x = static_cast<std::int32_t>(
                inst.dst_x0 * static_cast<float>(dst_image.width));
            const auto dst_y = static_cast<std::int32_t>(
                inst.dst_y0 * static_cast<float>(dst_image.height));

            if (dst_w <= 0 || dst_h <= 0) continue;

            const float opacity = inst.opacity;

            if (inst.transform_index > 0 &&
                (inst.transform_index + 1) * 16 <= transforms.size()) {
                // Affine transform path: use the matrix from the transform
                // table.  Each transform is 16 floats (column-major 4×4).
                runtime::SurfaceAffineTransform affine;
                const float* matrix = transforms.data() + inst.transform_index * 16;
                std::copy(matrix, matrix + 16, &affine.matrix[0]);
                transform_affine(destination, source_handle, affine);
            } else if (dst_x != 0 || dst_y != 0) {
                // Integer-offset transform (no scale/rotation)
                transform(destination, source_handle, dst_x, dst_y, opacity);
            } else if (opacity < 1.0f) {
                transform(destination, source_handle, 0, 0, opacity);
            } else {
                composite(destination, source_handle, inst.blend == BlendMode::Add
                          ? BlendMode::Add : BlendMode::Normal);
            }
        }
    }

    void ensure_images(std::uint32_t width, std::uint32_t height) {
        if (dst.width == width && dst.height == height && src.image != VK_NULL_HANDLE) return;
        destroy_image(dst);
        destroy_image(src);
        check(vkResetDescriptorPool(device, descriptor_pool, 0), "vkResetDescriptorPool");
        make_image(dst, width, height);
        make_image(src, width, height);
        descriptor_set = VK_NULL_HANDLE;
        glow_descriptor_sets = {};
        bind_descriptors(dst, src);
    }

    void ensure_staging(VkDeviceSize bytes) {
        if (staging_capacity >= bytes) return;
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, staging_memory, nullptr);
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &staging), "vkCreateBuffer(staging)");
        ++stats.staging_allocations;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &staging_memory), "vkAllocateMemory(staging)");
        check(vkBindBufferMemory(device, staging, staging_memory, 0), "vkBindBufferMemory");
        staging_capacity = bytes;
    }

    // Grow (or create) the device-local glyph-instance storage buffer.  The
    // buffer is updated in-band with vkCmdUpdateBuffer and read by the
    // text-run compute kernel, so it needs both transfer-dst and storage
    // usage; device-local memory keeps the GPU read path on-card.
    void ensure_glyph_instance_buffer(VkDeviceSize bytes, std::size_t index) {
        if (index >= kGlyphInstanceRingSize) index = 0;
        if (glyph_instance_capacities[index] >= bytes) return;
        if (glyph_instance_buffers[index] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, glyph_instance_buffers[index], nullptr);
        }
        if (glyph_instance_memories[index] != VK_NULL_HANDLE) {
            vkFreeMemory(device, glyph_instance_memories[index], nullptr);
        }
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &glyph_instance_buffers[index]),
              "vkCreateBuffer(glyph instances)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, glyph_instance_buffers[index], &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &glyph_instance_memories[index]),
              "vkAllocateMemory(glyph instances)");
        check(vkBindBufferMemory(device, glyph_instance_buffers[index], glyph_instance_memories[index], 0),
              "vkBindBufferMemory(glyph instances)");
        glyph_instance_capacities[index] = bytes;
        glyph_instance_hashes[index] = 0;
        glyph_instance_sizes[index] = 0;
    }

    static void transition(VkCommandBuffer command, VkImage image,
                           VkImageLayout old_layout, VkImageLayout new_layout) {
        const VkImageMemoryBarrier barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            old_layout, new_layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void begin_command_buffer() {
        wait_for_pending();
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
    }

    void wait_for_pending() {
        if (pending_timeline_value != 0) {
            const auto wait_start = profiling::now();
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            const auto wait_us = static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
            stats.gpu_wait_cpu_us += wait_us;
            ++stats.standalone_wait_count;
            stats.standalone_wait_us += wait_us;
            check(vkResetFences(device, 1, &fence), "vkResetFences");
            pending_timeline_value = 0;
        }
        // Synchronize any frame-batch slots still in flight.  This is a sync
        // point (release/readback path), distinct from begin_frame_batch()
        // which waits ONLY on the fence of the slot being reused.
        for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
            if (!frame_batch.in_flight[i]) continue;
            const auto wait_start = profiling::now();
            const VkResult wait_result = vkWaitForFences(
                device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX);
            if (wait_result == VK_ERROR_DEVICE_LOST) {
                spdlog::error(
                    "[vulkan] DEVICE LOST REPORT phase=wait_for_pending slot={} "
                    "in_flight_slots=[{},{},{}] pending_timeline={}",
                    i, frame_batch.in_flight[0], frame_batch.in_flight[1],
                    frame_batch.in_flight[2], pending_timeline_value);
            }
            check(wait_result, "vkWaitForFences(frame batch slot)");
            const auto wait_us = static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
            stats.gpu_wait_cpu_us += wait_us;
            ++stats.frame_batch_drain_wait_count;
            stats.frame_batch_drain_wait_us += wait_us;
            check(vkResetFences(device, 1, &frame_batch.fences[i]),
                  "vkResetFences(frame batch slot)");
            frame_batch.in_flight[i] = false;
            read_gpu_timestamps(i);
        }
        // Synchronize any in-flight upload slots too.  release_surface() and
        // every standalone path call wait_for_pending() before reusing or
        // destroying a surface, and an asynchronous upload may still be
        // copying into it; draining these slots closes the use-after-free
        // where an in-flight VkImage was destroyed before its copy completed.
        for (auto& slot : upload_slots) {
            wait_upload_slot(slot);
        }
    }

    std::uint64_t submit(bool wait_for_completion = true) {
        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
        const auto signal_value = ++next_timeline_value;
        std::vector<VkSemaphore> wait_semaphores;
        std::vector<VkPipelineStageFlags> wait_stages;
        std::vector<VkSemaphore> signal_semaphores{timeline_semaphore};
        std::vector<std::uint64_t> signal_values{signal_value};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        for (const auto physical_slot : cuda_ready_surfaces) {
            const auto it = physical_surfaces.find(physical_slot);
            if (it == physical_surfaces.end()) continue;
            wait_semaphores.push_back(it->second.image.cuda_to_vulkan);
            wait_stages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
            signal_values.push_back(0);
        }
        for (const auto physical_slot : cuda_export_ready_surfaces) {
            if (cuda_ready_surfaces.contains(physical_slot)) continue;
            const auto it = physical_surfaces.find(physical_slot);
            if (it == physical_surfaces.end()) continue;
            signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
            signal_values.push_back(0);
        }
#endif
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, static_cast<std::uint32_t>(signal_values.size()),
            signal_values.data()};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
            static_cast<std::uint32_t>(wait_semaphores.size()),
            wait_semaphores.data(), wait_stages.data(), 1, &command_buffer,
            static_cast<std::uint32_t>(signal_semaphores.size()),
            signal_semaphores.data()};
        const auto submit_start = profiling::now();
        check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
        stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        pending_timeline_value = signal_value;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        cuda_ready_surfaces.clear();
        cuda_export_ready_surfaces.clear();
#endif
        if (wait_for_completion) wait_for_pending();
        return signal_value;
    }

    void composite(Framebuffer& destination, const Framebuffer& source) {
        const auto width = static_cast<std::uint32_t>(destination.width());
        const auto height = static_cast<std::uint32_t>(destination.height());
        const VkDeviceSize image_bytes = static_cast<VkDeviceSize>(width) * height * sizeof(float) * 4;
        ensure_images(width, height);
        ensure_staging(image_bytes * 3);

        std::vector<float> packed(static_cast<std::size_t>(width) * height * 8);
        auto pack = [&](const Framebuffer& framebuffer, std::size_t offset) {
            std::size_t index = offset / sizeof(float);
            for (int y = 0; y < framebuffer.height(); ++y) {
                for (int x = 0; x < framebuffer.width(); ++x) {
                    const auto color = framebuffer.get_pixel(x, y);
                    packed[index++] = color.r;
                    packed[index++] = color.g;
                    packed[index++] = color.b;
                    packed[index++] = color.a;
                }
            }
        };
        pack(source, 0);
        pack(destination, static_cast<std::size_t>(image_bytes));
        void* mapped = nullptr;
        check(vkMapMemory(device, staging_memory, 0, image_bytes * 2, 0, &mapped), "vkMapMemory(upload)");
        std::memcpy(mapped, packed.data(), static_cast<std::size_t>(image_bytes * 2));
        vkUnmapMemory(device, staging_memory);

        const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
        transition(command_buffer, src.image,
                   src.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        transition(command_buffer, dst.image,
                   dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy source_copy{0, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        const VkBufferImageCopy destination_copy{image_bytes, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        vkCmdCopyBufferToImage(command_buffer, staging, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &source_copy);
        vkCmdCopyBufferToImage(command_buffer, staging, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &destination_copy);
        transition(command_buffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
        vkCmdDispatch(command_buffer, (width + 15) / 16, (height + 15) / 16, 1);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy output_copy{image_bytes * 2, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        vkCmdCopyImageToBuffer(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &output_copy);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        src.initialized = true;
        dst.initialized = true;
        submit();

        check(vkMapMemory(device, staging_memory, image_bytes * 2, image_bytes, 0, &mapped), "vkMapMemory(readback)");
        const float* output = static_cast<const float*>(mapped);
        for (int y = 0; y < destination.height(); ++y) {
            for (int x = 0; x < destination.width(); ++x) {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
                destination.set_pixel(x, y, Color{output[index], output[index + 1], output[index + 2], output[index + 3]});
            }
        }
        vkUnmapMemory(device, staging_memory);
    }
};
