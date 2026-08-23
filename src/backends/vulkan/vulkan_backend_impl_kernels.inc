// Vulkan Impl kernel recording and submission helpers. Included inside VulkanBackend::Impl to keep
// the private state definition single-source while separating responsibilities.
    void record_composite(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          std::int32_t blend_mode, float source_scale,
                          const float tint[4],
                          const std::optional<raster::BBox>& clip = std::nullopt) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t blend_mode;
            float source_scale;
            // GLSL std140 push-constant alignment places the vec4 tint at
            // byte 16 and the clip rectangle at byte 32.
            float padding[2];
            float tint[4];
            float clip_rect[4];
            std::int32_t dispatch_origin[2];
        } params{blend_mode, source_scale, {0.0f, 0.0f},
                 {tint[0], tint[1], tint[2], tint[3]},
                 {0.0f, 0.0f, static_cast<float>(destination.width),
                  static_cast<float>(destination.height)}, {0, 0}};
        if (clip) {
            params.clip_rect[0] = static_cast<float>(std::max(0, clip->x0));
            params.clip_rect[1] = static_cast<float>(std::max(0, clip->y0));
            params.clip_rect[2] = static_cast<float>(std::min(
                static_cast<std::int32_t>(destination.width), clip->x1));
            params.clip_rect[3] = static_cast<float>(std::min(
                static_cast<std::int32_t>(destination.height), clip->y1));
        }
        const auto x0 = std::clamp(static_cast<std::int32_t>(params.clip_rect[0]),
                                   0, static_cast<std::int32_t>(destination.width));
        const auto y0 = std::clamp(static_cast<std::int32_t>(params.clip_rect[1]),
                                   0, static_cast<std::int32_t>(destination.height));
        const auto x1 = std::clamp(static_cast<std::int32_t>(params.clip_rect[2]),
                                   x0, static_cast<std::int32_t>(destination.width));
        const auto y1 = std::clamp(static_cast<std::int32_t>(params.clip_rect[3]),
                                   y0, static_cast<std::int32_t>(destination.height));
        params.dispatch_origin[0] = x0;
        params.dispatch_origin[1] = y0;
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_transform(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          int offset_x, int offset_y, float opacity) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Transform)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t offset_x;
            std::int32_t offset_y;
            float opacity;
            float padding;
        } push{offset_x, offset_y, opacity, 0.0f};
        vkCmdPushConstants(command, pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_transform_affine(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 const Image& destination, const Image& source,
                                 runtime::SurfaceAffineTransform transform) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::AffineTransform)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);

        std::int32_t x0 = 0;
        std::int32_t y0 = 0;
        std::int32_t x1 = static_cast<std::int32_t>(destination.width);
        std::int32_t y1 = static_cast<std::int32_t>(destination.height);

        if (transform.clip_enabled != 0u) {
            const std::int32_t clip_x0 = transform.clip_rect[0] - transform.destination_origin_x;
            const std::int32_t clip_y0 = transform.clip_rect[1] - transform.destination_origin_y;
            const std::int32_t clip_x1 = transform.clip_rect[2] - transform.destination_origin_x;
            const std::int32_t clip_y1 = transform.clip_rect[3] - transform.destination_origin_y;

            x0 = std::clamp(clip_x0, 0, static_cast<std::int32_t>(destination.width));
            y0 = std::clamp(clip_y0, 0, static_cast<std::int32_t>(destination.height));
            x1 = std::clamp(clip_x1, x0, static_cast<std::int32_t>(destination.width));
            y1 = std::clamp(clip_y1, y0, static_cast<std::int32_t>(destination.height));
        }

        transform.dispatch_origin_x = x0;
        transform.dispatch_origin_y = y0;

        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(transform), &transform);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_blur(VkCommandBuffer command, VkDescriptorSet descriptors,
                     const Image& destination, const Image& source,
                     float radius, bool horizontal) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Blur)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float radius;
            std::int32_t horizontal;
        } params{radius, horizontal ? 1 : 0};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_color_adjust(VkCommandBuffer command, VkDescriptorSet descriptors,
                             const Image& destination, const Image& source,
                             float brightness, float contrast,
                             const Color& tint, float tint_amount) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::ColorAdjust)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float brightness;
            float contrast;
            float tint_amount;
            float padding;
            float tint[4];
        } params{brightness, contrast, tint_amount, 0.0f,
                 {tint.r, tint.g, tint.b, tint.a}};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_matte(VkCommandBuffer command, VkDescriptorSet descriptors,
                      const Image& destination, const Image& target,
                      const Image& matte, bool luma, bool inverted) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Matte)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t luma;
            std::int32_t inverted;
        } params{luma ? 1 : 0, inverted ? 1 : 0};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_fill_rect(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination,
                          std::int32_t x0, std::int32_t y0,
                          std::int32_t x1, std::int32_t y1,
                          const Color& color) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::FillRect)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t rect[4];
            float color[4];
        } params{{x0, y0, x1, y1}, {color.r, color.g, color.b, color.a}};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        const auto rx0 = std::clamp(x0, 0, static_cast<std::int32_t>(destination.width));
        const auto ry0 = std::clamp(y0, 0, static_cast<std::int32_t>(destination.height));
        const auto rx1 = std::clamp(x1, rx0, static_cast<std::int32_t>(destination.width));
        const auto ry1 = std::clamp(y1, ry0, static_cast<std::int32_t>(destination.height));
        vkCmdDispatch(command, (rx1 - rx0 + 15) / 16,
                      (ry1 - ry0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_text_run(VkCommandBuffer command, VkDescriptorSet descriptors,
                         const Image& destination, std::int32_t glyph_count,
                         VkBuffer instance_buffer, bool instance_updated,
                         float current_frame, const Color& highlight_color,
                         bool highlight_enabled,
                         std::int32_t dispatch_origin_x,
                         std::int32_t dispatch_origin_y,
                         std::int32_t dispatch_end_x,
                         std::int32_t dispatch_end_y) {
        // Multiple TextRun passes can occur in one frame (for example the
        // watermark followed by subtitles). They intentionally share the
        // per-frame instance buffer, but the previous dispatch may still be
        // reading it when the next vkCmdUpdateBuffer records its write.
        // Make that shader-read -> transfer-write dependency explicit before
        // publishing the new instance data below. Without this barrier the
        // hazard that can surface later as VK_ERROR_DEVICE_LOST.
        if (instance_updated) {
            const VkBufferMemoryBarrier reuse_barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                instance_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 1, &reuse_barrier, 0, nullptr);
        }
        // The glyph instances were written by a preceding vkCmdUpdateBuffer
        // (transfer stage); publish them to the compute shader before dispatch.
        if (instance_updated) {
            const VkBufferMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                instance_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 0, nullptr, 1, &barrier, 0, nullptr);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::TextRun)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t glyph_count;
            float current_frame;
            std::int32_t highlight_enabled;
            std::int32_t padding;
            float highlight_color[4];
            std::int32_t dispatch_origin[2];
        } params{glyph_count, current_frame, highlight_enabled ? 1 : 0, 0,
                 {highlight_color.r, highlight_color.g,
                  highlight_color.b, highlight_color.a}, {0, 0}};
        const auto x0 = std::clamp(dispatch_origin_x, 0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y0 = std::clamp(dispatch_origin_y, 0,
                                   static_cast<std::int32_t>(destination.height));
        const auto x1 = std::clamp(dispatch_end_x, x0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y1 = std::clamp(dispatch_end_y, y0,
                                   static_cast<std::int32_t>(destination.height));
        params.dispatch_origin[0] = x0;
        params.dispatch_origin[1] = y0;
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_layer_batch(VkCommandBuffer command, VkDescriptorSet descriptors,
                            const Image& destination, std::int32_t instance_count,
                            VkBuffer instance_buffer, bool instance_updated,
                            std::int32_t dispatch_origin_x,
                            std::int32_t dispatch_origin_y,
                            std::int32_t dispatch_end_x,
                            std::int32_t dispatch_end_y) {
        if (instance_updated) {
            const VkBufferMemoryBarrier reuse_barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                instance_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 1, &reuse_barrier, 0, nullptr);
            const VkBufferMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                instance_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 0, nullptr, 1, &barrier, 0, nullptr);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::LayerBatch)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t instance_count;
            std::int32_t padding0;
            std::int32_t dispatch_origin[2];
        } params{instance_count, 0, {dispatch_origin_x, dispatch_origin_y}};
        const auto x0 = std::clamp(dispatch_origin_x, 0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y0 = std::clamp(dispatch_origin_y, 0,
                                   static_cast<std::int32_t>(destination.height));
        const auto x1 = std::clamp(dispatch_end_x, x0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y1 = std::clamp(dispatch_end_y, y0,
                                   static_cast<std::int32_t>(destination.height));
        params.dispatch_origin[0] = x0;
        params.dispatch_origin[1] = y0;
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_text_batch(VkCommandBuffer command, VkDescriptorSet descriptors,
                           const Image& destination, std::int32_t glyph_count,
                           std::int32_t run_count,
                           VkBuffer glyph_buffer, bool glyph_updated,
                           VkBuffer run_buffer, bool run_updated,
                           std::int32_t dispatch_origin_x,
                           std::int32_t dispatch_origin_y,
                           std::int32_t dispatch_end_x,
                           std::int32_t dispatch_end_y) {
        if (glyph_updated) {
            const VkBufferMemoryBarrier reuse_barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                glyph_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 1, &reuse_barrier, 0, nullptr);
            const VkBufferMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                glyph_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 0, nullptr, 1, &barrier, 0, nullptr);
        }
        if (run_updated) {
            const VkBufferMemoryBarrier reuse_barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                run_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 1, &reuse_barrier, 0, nullptr);
            const VkBufferMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                run_buffer, 0, VK_WHOLE_SIZE};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 0, nullptr, 1, &barrier, 0, nullptr);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::TextBatch)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t glyph_count;
            std::int32_t run_count;
            std::int32_t dispatch_origin[2];
        } params{glyph_count, run_count, {dispatch_origin_x, dispatch_origin_y}};
        const auto x0 = std::clamp(dispatch_origin_x, 0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y0 = std::clamp(dispatch_origin_y, 0,
                                   static_cast<std::int32_t>(destination.height));
        const auto x1 = std::clamp(dispatch_end_x, x0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y1 = std::clamp(dispatch_end_y, y0,
                                   static_cast<std::int32_t>(destination.height));
        params.dispatch_origin[0] = x0;
        params.dispatch_origin[1] = y0;
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_text_tile_bin(VkCommandBuffer command, VkDescriptorSet descriptors,
                              std::int32_t glyph_count, std::int32_t tiles_x,
                              std::int32_t tiles_y, std::int32_t max_glyphs_per_tile,
                              std::int32_t dst_width, std::int32_t dst_height) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(
                              GpuKernelId::TextTileBin)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                text_tile_bin_pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::uint32_t glyph_count;
            std::uint32_t tiles_x;
            std::uint32_t tiles_y;
            std::uint32_t max_glyphs_per_tile;
            std::uint32_t dst_width;
            std::uint32_t dst_height;
        } params{static_cast<std::uint32_t>(glyph_count), static_cast<std::uint32_t>(tiles_x),
                 static_cast<std::uint32_t>(tiles_y), static_cast<std::uint32_t>(max_glyphs_per_tile),
                 static_cast<std::uint32_t>(dst_width), static_cast<std::uint32_t>(dst_height)};
        vkCmdPushConstants(command, text_tile_bin_pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, (static_cast<std::uint32_t>(glyph_count) + 63) / 64, 1, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void record_text_tile_raster(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 std::int32_t tiles_x, std::int32_t tiles_y,
                                 std::int32_t max_glyphs_per_tile,
                                 const Image& destination, const Image& atlas) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(
                              GpuKernelId::TextTileRaster)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                text_tile_raster_pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::uint32_t tiles_x;
            std::uint32_t tiles_y;
            std::uint32_t max_glyphs_per_tile;
            std::uint32_t dst_width;
            std::uint32_t dst_height;
            float atlas_inv_w;
            float atlas_inv_h;
        } params{static_cast<std::uint32_t>(tiles_x), static_cast<std::uint32_t>(tiles_y),
                 static_cast<std::uint32_t>(max_glyphs_per_tile), destination.width,
                 destination.height, 1.0f / static_cast<float>(atlas.width),
                 1.0f / static_cast<float>(atlas.height)};
        vkCmdPushConstants(command, text_tile_raster_pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, static_cast<std::uint32_t>(tiles_x),
                      static_cast<std::uint32_t>(tiles_y), 1);
        ++stats.vk_cmd_dispatch_count;
    }

    // Read the frame's [start, end] timestamp pair for a ring slot after its
    // fence has been signaled and accumulate the GPU elapsed duration.  The
    // slot's queries are reset by vkCmdResetQueryPool when the buffer is
    // re-recorded, so this must run after the fence wait and before that
    // reset executes on the GPU.
    void read_gpu_timestamps(std::size_t slot) {
        if (timestamp_pool == VK_NULL_HANDLE) return;
        std::uint64_t stamps[2] = {0, 0};
        const VkResult result = vkGetQueryPoolResults(
            device, timestamp_pool, static_cast<std::uint32_t>(2 * slot), 2,
            sizeof(stamps), stamps, sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result != VK_SUCCESS) return;
        if (stamps[1] >= stamps[0]) {
            const double elapsed_ns =
                static_cast<double>(stamps[1] - stamps[0]) * timestamp_period_ns;
            stats.gpu_execute_us += static_cast<std::uint64_t>(elapsed_ns / 1000.0);
        }
        // Fase 6: real GPU bars on the "Chronon Vulkan Queue" track.  Only
        // when VK_EXT_calibrated_timestamps provided an anchor (calibration
        // taken at backend construction); otherwise only the CPU-side
        // VulkanSubmit/FenceWait events are traced — never fake GPU bars.
        if (gpu_timestamps_calibrated && stamps[0] >= calibration_gpu_ts &&
            stamps[1] > stamps[0]) {
            const auto gpu_to_cpu = [this](std::uint64_t gpu) {
                return calibration_cpu_trace_ns +
                    static_cast<std::int64_t>(
                        static_cast<double>(gpu - calibration_gpu_ts) *
                        timestamp_period_ns);
            };
            const auto start_ns = gpu_to_cpu(stamps[0]);
            const auto end_ns = gpu_to_cpu(stamps[1]);
            if (end_ns > start_ns) {
                CHRONON_TRACE_GPU_BEGIN("VulkanExecute", start_ns);
                CHRONON_TRACE_GPU_END(end_ns);
            }
        }
    }

    // End the active frame batch's command buffer and submit it exactly once
    // with the current slot's fence.  No wait-for-completion happens here:
    // the caller waits only when that slot is reused (begin_frame_batch())
    // or before a readback (wait_for_pending()).
    void submit_batch() {
        const auto slot = frame_batch.next_slot;
        if (timestamp_pool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(frame_batch.command_buffers[slot],
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                timestamp_pool,
                                static_cast<std::uint32_t>(2 * slot + 1));
        }
        check(vkEndCommandBuffer(frame_batch.command_buffers[slot]),
              "vkEndCommandBuffer(frame batch)");
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
            // A reused surface is already waiting on CUDA completion above;
            // that path also emits the single Vulkan->CUDA release signal.
            // Do not signal the same binary semaphore twice in one submit.
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
            wait_semaphores.data(), wait_stages.data(),
            1, &frame_batch.command_buffers[slot],
            static_cast<std::uint32_t>(signal_semaphores.size()),
            signal_semaphores.data()};
        const auto submit_start = profiling::now();
        CHRONON_TRACE_SCOPE("chronon.gpu", "VulkanSubmit");
        check(vkQueueSubmit(queue, 1, &submit_info, frame_batch.fences[slot]),
              "vkQueueSubmit(frame batch)");
        stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        frame_batch.in_flight[slot] = true;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        cuda_ready_surfaces.clear();
        cuda_export_ready_surfaces.clear();
#endif
        frame_batch.pass_count = 0;
        frame_batch.next_slot = (slot + 1) % FrameBatchState::kSlotCount;
    }

    // ── Command-replay: record once, submit with param writes ────────
    //
    // prepare() calls begin_replay_recording() / end_replay_recording()
    // for each pre-planned pass to bake a VkCommandBuffer.  At frame time
    // replay_submit() writes the per-frame params into the slot's mapped
    // buffer and submits the pre-recorded command buffer — zero vkCmd*
    // calls in the render loop.
    //
    // The params buffer is a simple flat allocation; the caller is
    // responsible for the layout (typically a struct matching the shader's
    // uniform block).  Capacity grows on demand but never shrinks.

    void ensure_replay_params_capacity(ReplaySlot& slot, VkDeviceSize bytes) {
        if (slot.params_capacity >= bytes) return;
        // Destroy old buffer if it exists.
        if (slot.params_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, slot.params_buffer, nullptr);
            vkFreeMemory(device, slot.params_memory, nullptr);
            slot.mapped_params = nullptr;
        }
        const VkBufferCreateInfo buffer_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0,
            bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &buffer_info, nullptr, &slot.params_buffer),
              "vkCreateBuffer(replay params)");
        VkMemoryRequirements mem_reqs{};
        vkGetBufferMemoryRequirements(device, slot.params_buffer, &mem_reqs);
        const auto alloc = memory_arena.allocate(
            mem_reqs, MemoryClass::HostVisible);
        if (alloc.memory == VK_NULL_HANDLE) {
            throw std::runtime_error("replay params allocation failed");
        }
        slot.params_memory = alloc.memory;
        check(vkBindBufferMemory(device, slot.params_buffer,
                                 slot.params_memory, alloc.offset),
              "vkBindBufferMemory(replay params)");
        check(vkMapMemory(device, slot.params_memory, 0, bytes, 0,
                          &slot.mapped_params),
              "vkMapMemory(replay params)");
        slot.params_capacity = bytes;
    }

    /// Open a replay slot for recording.  The caller records all commands
    /// for one frame into the returned command buffer, then calls
    /// end_replay_recording().  Must not be called while a frame batch
    /// or another replay recording is active.
    VkCommandBuffer begin_replay_recording(std::size_t slot_index) {
        if (slot_index >= kReplaySlotCount) {
            throw std::out_of_range("begin_replay_recording: slot out of range");
        }
        auto& slot = replay_slots[slot_index];
        if (slot.command_buffer == VK_NULL_HANDLE) {
            const VkCommandBufferAllocateInfo alloc_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
                command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
            check(vkAllocateCommandBuffers(device, &alloc_info,
                                           &slot.command_buffer),
                  "vkAllocateCommandBuffers(replay slot)");
        }
        if (slot.fence == VK_NULL_HANDLE) {
            const VkFenceCreateInfo fence_info{
                VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            check(vkCreateFence(device, &fence_info, nullptr, &slot.fence),
                  "vkCreateFence(replay slot)");
        }
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, nullptr};
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(replay slot)");
        return slot.command_buffer;
    }

    /// Close the replay slot's command buffer.  After this call the slot
    /// holds a pre-recorded, reusable VkCommandBuffer.
    void end_replay_recording(std::size_t slot_index) {
        if (slot_index >= kReplaySlotCount) {
            throw std::out_of_range("end_replay_recording: slot out of range");
        }
        auto& slot = replay_slots[slot_index];
        check(vkEndCommandBuffer(slot.command_buffer),
              "vkEndCommandBuffer(replay slot)");
    }

    /// Submit a pre-recorded replay slot with the given per-frame params.
    /// Waits on the slot's fence if it's still in flight (same ring-depth
    /// as the frame batch), writes `params` into the mapped buffer, then
    /// calls vkQueueSubmit exactly once.
    void replay_submit(std::size_t slot_index,
                       const void* params, VkDeviceSize params_size) {
        if (slot_index >= kReplaySlotCount) {
            throw std::out_of_range("replay_submit: slot out of range");
        }
        auto& slot = replay_slots[slot_index];
        // Wait for previous frame using this slot.
        if (slot.in_flight) {
            CHRONON_TRACE_SCOPE("chronon.gpu", "ReplayFenceWait");
            check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(replay slot)");
            check(vkResetFences(device, 1, &slot.fence),
                  "vkResetFences(replay slot)");
            slot.in_flight = false;
        }
        // Write per-frame params into the persistently-mapped buffer.
        if (params && params_size > 0) {
            ensure_replay_params_capacity(slot, params_size);
            std::memcpy(slot.mapped_params, params,
                        static_cast<std::size_t>(params_size));
        }
        // Single submit of the pre-recorded command buffer.
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
            0, nullptr, nullptr,
            1, &slot.command_buffer,
            1, &timeline_semaphore};
        const auto submit_start = profiling::now();
        CHRONON_TRACE_SCOPE("chronon.gpu", "ReplaySubmit");
        check(vkQueueSubmit(queue, 1, &submit_info, slot.fence),
              "vkQueueSubmit(replay slot)");
        stats.gpu_submit_cpu_us +=
            static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        slot.in_flight = true;
    }
