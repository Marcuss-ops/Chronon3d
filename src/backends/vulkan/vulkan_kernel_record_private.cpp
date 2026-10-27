// vulkan_kernel_record_private.cpp — VulkanBackend::Impl compute-kernel
// recording primitives (filter, warp/lut and color op recording).
// Out-of-class definitions; declared in vulkan_backend_impl.hpp.
// Composite recording lives in vulkan_kernel_record_composite_private.cpp,
// text recording in vulkan_kernel_record_text_private.cpp, replay/timing/
// submission in vulkan_kernel_replay_private.cpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

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

    void VulkanBackend::Impl::record_dof_depth_stamp(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& depth, const Image& layer,
                      const DofDepthStampPush& push) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::DofDepthStamp)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (depth.width + 15) / 16,
                      (depth.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
        (void)layer;
    }

    void VulkanBackend::Impl::record_dof_blur(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const Image& depth, const DofBlurPush& push) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::DofBlur)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
        (void)source;
        (void)depth;
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
        static_assert(sizeof(bulk) == 128, "GenerateBulk push must be 128 bytes");
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
        params.options[0] = static_cast<float>(transform.mode);
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
} // namespace chronon3d::backends::vulkan
