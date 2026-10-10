// vulkan_kernel_record_private.cpp — VulkanBackend::Impl compute-kernel
// recording primitives (per-pass command recording for each operation type).
// Out-of-class definitions; declared in vulkan_backend_impl.hpp.
// Replay/timing/submission live in vulkan_kernel_replay_private.cpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

namespace {

void emit_buffer_barrier2(VkDevice device, VkCommandBuffer command, VkBuffer buffer,
                          VkPipelineStageFlags2KHR src_stage, VkAccessFlags2KHR src_access,
                          VkPipelineStageFlags2KHR dst_stage, VkAccessFlags2KHR dst_access) {
    if (buffer == VK_NULL_HANDLE) return;
    const auto pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
    if (!pipeline_barrier2) {
        throw std::runtime_error(
            "Vulkan: vkCmdPipelineBarrier2KHR unavailable after synchronization2 enablement");
    }
    const VkBufferMemoryBarrier2KHR barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR,
        nullptr,
        src_stage,
        src_access,
        dst_stage,
        dst_access,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        buffer,
        0,
        VK_WHOLE_SIZE};
    const VkDependencyInfoKHR dependency{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        nullptr,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr};
    pipeline_barrier2(command, &dependency);
}

} // namespace

void VulkanBackend::Impl::record_composite(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        std::int32_t blend_mode, float source_scale,
        const float tint[4],
        const std::optional<raster::BBox>& clip,
        bool premultiplied_source_over) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t blend_mode;
            float source_scale;
            // GLSL std140 push-constant alignment places the vec4 tint at
            // byte 16 and the clip rectangle at byte 32.
            float padding[2];
            float tint[4];
            float clip_rect[4];
            std::int32_t dispatch_origin[2];
            std::int32_t premultiplied_source_over;
            std::int32_t push_padding;
        } params{blend_mode, source_scale, {0.0f, 0.0f},
                 {tint[0], tint[1], tint[2], tint[3]},
                 {0.0f, 0.0f, static_cast<float>(destination.width),
                  static_cast<float>(destination.height)}, {0, 0},
                 premultiplied_source_over ? 1 : 0, 0};
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
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_transform(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          int offset_x, int offset_y, float opacity) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Transform)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t offset_x;
            std::int32_t offset_y;
            float opacity;
            float padding;
        } push{offset_x, offset_y, opacity, 0.0f};
        vkCmdPushConstants(command, kernels.general_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_transform_affine(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 const Image& destination, const Image& source,
                                 runtime::SurfaceAffineTransform transform) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::AffineTransform)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);

        std::int32_t x0 = 0;
        std::int32_t y0 = 0;
        std::int32_t x1 = static_cast<std::int32_t>(destination.width);
        std::int32_t y1 = static_cast<std::int32_t>(destination.height);

        if (transform.clip_enabled != 0u) {
            const std::int32_t clip_x0 = transform.clip_rect[0];
            const std::int32_t clip_y0 = transform.clip_rect[1];
            const std::int32_t clip_x1 = transform.clip_rect[2];
            const std::int32_t clip_y1 = transform.clip_rect[3];

            x0 = std::clamp(clip_x0, 0, static_cast<std::int32_t>(destination.width));
            y0 = std::clamp(clip_y0, 0, static_cast<std::int32_t>(destination.height));
            x1 = std::clamp(clip_x1, x0, static_cast<std::int32_t>(destination.width));
            y1 = std::clamp(clip_y1, y0, static_cast<std::int32_t>(destination.height));
        }

        transform.dispatch_origin_x = x0;
        transform.dispatch_origin_y = y0;

        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(transform), &transform);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_blur(VkCommandBuffer command, VkDescriptorSet descriptors,
                     const Image& destination, const Image& source,
                     float radius, bool horizontal, bool box_kernel,
                     bool glow_kernel) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Blur)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float radius;
            std::int32_t horizontal;
            std::int32_t box_kernel;
        } params{radius, horizontal ? 1 : 0,
                 box_kernel ? 1 : (glow_kernel ? 2 : 0)};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_directional_blur(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      float dir_x, float dir_y, float length, int tap_count) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::DirectionalBlur)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float dir_x;
            float dir_y;
            float length;
            std::int32_t tap_count;
        } params{dir_x, dir_y, length, tap_count};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_radial_blur(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      float center_x_px, float center_y_px,
                      float amount, int tap_count) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::RadialBlur)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float center_x_px;
            float center_y_px;
            float amount;
            std::int32_t tap_count;
        } params{center_x_px, center_y_px, amount, tap_count};
        static_assert(sizeof(params) == 16, "RadialBlur push must be 16 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_warp_bulk(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const graph::WarpBulkParams& bulk) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::WarpBulk)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        static_assert(sizeof(bulk) == 128, "WarpBulk push must be 128 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(bulk), &bulk);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_generate_bulk(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const graph::GenerateBulkParams& bulk) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::GenerateBulk)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        static_assert(sizeof(bulk) == 120, "GenerateBulk push must be 120 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(bulk), &bulk);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_lut_bulk(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const graph::LutBulkParams& bulk) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::LutBulk)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        static_assert(sizeof(bulk) == 64, "LutBulk push must be 64 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(bulk), &bulk);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_sharpen_combine(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const Image& blurred, float amount, float threshold) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Sharpen)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float amount;
            float threshold;
        } params{amount, threshold};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_bloom_threshold(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        float threshold) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::BloomThreshold)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float threshold;
            float padding;
            float dispatch_origin[2];
        } push{threshold, 0.0f, {0.0f, 0.0f}};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_glow_prepare(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        float intensity, const Color& tint) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::GlowPrepare)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float tint[4];
            float intensity;
            float padding[3];
        } push{{tint.r, tint.g, tint.b, tint.a}, intensity, {0.0f, 0.0f, 0.0f}};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_color_adjust(VkCommandBuffer command, VkDescriptorSet descriptors,
                             const Image& destination, const Image& source,
                             float brightness, float contrast,
                             const Color& tint, float tint_amount) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::ColorAdjust)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float brightness;
            float contrast;
            float tint_amount;
            float padding;
            float tint[4];
        } params{brightness, contrast, tint_amount, 0.0f,
                 {tint.r, tint.g, tint.b, tint.a}};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_color_transform(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& /*source*/,
        const graph::NativeColorTransform& transform) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::ColorMatrix)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float p[5][4];
            float options[4];
        } params{};
        static_assert(sizeof(PushConstants) == 96);
        for (int row = 0; row < 5; ++row)
            for (int lane = 0; lane < 4; ++lane)
                params.p[row][lane] = transform.p[static_cast<std::size_t>(row)][static_cast<std::size_t>(lane)];
        params.options[0] = transform.mode == graph::NativeColorTransform::Mode::Levels ? 1.0f :
                            (transform.mode == graph::NativeColorTransform::Mode::Threshold ? 2.0f :
                             (transform.mode == graph::NativeColorTransform::Mode::Keying ? 3.0f :
                              (transform.mode == graph::NativeColorTransform::Mode::Vignette ? 4.0f :
                               (transform.mode == graph::NativeColorTransform::Mode::Mosaic ? 5.0f :
                                (transform.mode == graph::NativeColorTransform::Mode::LiftGammaGain ? 6.0f :
                                 (transform.mode == graph::NativeColorTransform::Mode::Offset ? 7.0f :
                                  (transform.mode == graph::NativeColorTransform::Mode::Noise ? 8.0f :
                                   (transform.mode == graph::NativeColorTransform::Mode::Checkerboard ? 9.0f : 0.0f))))))));
        params.options[1] = transform.clamp_unit ? 1.0f : 0.0f;
        params.options[2] = transform.straight_alpha ? 1.0f : 0.0f;
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_chromatic_transform(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& /*source*/,
        const ChromaticAberrationParams& transform) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(
                              GpuKernelId::ChromaticAberration)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants { float values[32]; } params{};
        static_assert(sizeof(PushConstants) == 128);
        const auto channel = [&](std::size_t index, const RgbChannelTransform& value) {
            const std::size_t offset = index * 8;
            params.values[offset + 0] = value.translation.x;
            params.values[offset + 1] = value.translation.y;
            params.values[offset + 2] = value.scale.x;
            params.values[offset + 3] = value.scale.y;
            params.values[offset + 4] = value.rotation_degrees;
            params.values[offset + 5] = value.opacity;
        };
        channel(0, transform.red);
        channel(1, transform.green);
        channel(2, transform.blue);
        params.values[24] = transform.red_offset;
        params.values[25] = transform.blue_offset;
        params.values[26] = static_cast<float>(transform.offset_mode);
        params.values[28] = transform.center.x;
        params.values[29] = transform.center.y;
        params.values[30] = transform.velocity.x;
        params.values[31] = transform.velocity.y;
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_lighting(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        const Color& multiplier) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Lighting)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float multiplier[4];
        } params{{multiplier.r, multiplier.g, multiplier.b, multiplier.a}};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_matte(VkCommandBuffer command, VkDescriptorSet descriptors,
                      const Image& destination, const Image& target,
                      const Image& matte, bool luma, bool inverted) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Matte)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t luma;
            std::int32_t inverted;
        } params{luma ? 1 : 0, inverted ? 1 : 0};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_fill_rect(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination,
                          std::int32_t x0, std::int32_t y0,
                          std::int32_t x1, std::int32_t y1,
                          const Color& color,
                          const Vec4& shape,
                          const Vec4& line,
                          const std::array<Vec2, 8>& vertices) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::FillRect)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t rect[4];
            float color[4];
            float shape[4];
            float line[4];
            float vertices[16];
        } params{{x0, y0, x1, y1}, {color.r, color.g, color.b, color.a},
                 {shape.x, shape.y, shape.z, shape.w},
                 {line.x, line.y, line.z, line.w}, {}};
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            params.vertices[i * 2] = vertices[i].x;
            params.vertices[i * 2 + 1] = vertices[i].y;
        }
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        const auto rx0 = std::clamp(x0, 0, static_cast<std::int32_t>(destination.width));
        const auto ry0 = std::clamp(y0, 0, static_cast<std::int32_t>(destination.height));
        const auto rx1 = std::clamp(x1, rx0, static_cast<std::int32_t>(destination.width));
        const auto ry1 = std::clamp(y1, ry0, static_cast<std::int32_t>(destination.height));
        vkCmdDispatch(command, (rx1 - rx0 + 15) / 16,
                      (ry1 - ry0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_text_run(VkCommandBuffer command, VkDescriptorSet descriptors,
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
        // Keep both halves of that dependency on Synchronization2.
        if (instance_updated) {
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::TextRun)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
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
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_layer_batch(VkCommandBuffer command, VkDescriptorSet descriptors,
                            const Image& destination, std::int32_t instance_count,
                            VkBuffer instance_buffer, bool instance_updated,
                            std::int32_t dispatch_origin_x,
                            std::int32_t dispatch_origin_y,
                            std::int32_t dispatch_end_x,
                            std::int32_t dispatch_end_y) {
        if (instance_updated) {
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::LayerBatch)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
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
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_text_tile_bin(VkCommandBuffer command, VkDescriptorSet descriptors,
                              std::int32_t glyph_count, std::int32_t tiles_x,
                              std::int32_t tiles_y, std::int32_t max_glyphs_per_tile,
                              std::int32_t dst_width, std::int32_t dst_height) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(
                              GpuKernelId::TextTileBin)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.text_tile_bin_layout, 0, 1, &descriptors, 0, nullptr);
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
        vkCmdPushConstants(command, kernels.text_tile_bin_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, (static_cast<std::uint32_t>(glyph_count) + 63) / 64, 1, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_text_tile_raster(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 std::int32_t tiles_x, std::int32_t tiles_y,
                                 std::int32_t max_glyphs_per_tile,
                                 const Image& destination, const Image& atlas) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(
                              GpuKernelId::TextTileRaster)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.text_tile_raster_layout, 0, 1, &descriptors, 0, nullptr);
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
        vkCmdPushConstants(command, kernels.text_tile_raster_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, static_cast<std::uint32_t>(tiles_x),
                      static_cast<std::uint32_t>(tiles_y), 1);
        ++stats.vk_cmd_dispatch_count;
    }

} // namespace chronon3d::backends::vulkan
