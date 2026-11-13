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

    void VulkanBackend::Impl::record_occlusion_tag(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& mask, const Image& layer_depth,
                      const Image& frame_depth, std::int32_t merge) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::OcclusionTag)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(merge), &merge);
        vkCmdDispatch(command, (mask.width + 15) / 16, (mask.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
        (void)layer_depth;
        (void)frame_depth;
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

    void VulkanBackend::Impl::record_camera_ring(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const CameraRingParams& params) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::CameraRing)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float center_x, center_y;
            float axis_x, axis_y;
            float inner_radius, outer_radius, max_blur_radius, chroma_dispersion;
            float anamorphic_stretch;
            std::int32_t sample_count;
            std::int32_t pad0, pad1;
        } push{params.center.x, params.center.y,
               params.axis_scale.x, params.axis_scale.y,
               params.inner_radius, params.outer_radius,
               params.max_blur_radius, params.chroma_dispersion,
               params.anamorphic_stretch,
               static_cast<std::int32_t>(params.samples), 0, 0};
        static_assert(sizeof(push) == 48, "CameraRing push must be 48 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
        (void)source;
    }

    void VulkanBackend::Impl::record_sdf_bevel(VkCommandBuffer command,
                      VkDescriptorSet descriptors,
                      const Image& destination, const Image& source,
                      const SdfBevelPush& push) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::SdfBevel)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        const int mode = static_cast<int>(push.mode + 0.5f);
        std::uint32_t groups_x = 0;
        std::uint32_t groups_y = 1;
        if (mode == 4) {
            groups_x = static_cast<std::uint32_t>((destination.width + 63) / 64);
            groups_y = static_cast<std::uint32_t>(destination.height);
        } else if (mode <= 1) {
            groups_x = static_cast<std::uint32_t>((destination.height + 63) / 64);
        } else {
            groups_x = static_cast<std::uint32_t>((destination.width + 63) / 64);
        }
        vkCmdDispatch(command, groups_x, groups_y, 1);
        ++stats.vk_cmd_dispatch_count;
        (void)source;
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

    void VulkanBackend::Impl::record_bloom_prefilter(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        float threshold, float soft_knee) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::BloomPrefilter)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float threshold;
            float soft_knee;
            std::int32_t first_pass;
            std::int32_t padding;
        } push{threshold, soft_knee, 1, 0};
        static_assert(sizeof(push) == 16, "Bloom prefilter push must be 16 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_bloom_downsample(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::BloomDownsample)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_bloom_upsample(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        float filter_radius, float intensity, bool additive) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::BloomUpsample)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float filter_radius;
            float intensity;
            std::int32_t additive;
            std::int32_t padding;
        } push{filter_radius, intensity, additive ? 1 : 0, 0};
        static_assert(sizeof(push) == 16, "Bloom upsample push must be 16 bytes");
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_bloom_composite(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source, float intensity) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::BloomComposite)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float intensity;
            float padding[3];
        } push{intensity, {0.0f, 0.0f, 0.0f}};
        static_assert(sizeof(push) == 16, "Bloom composite push must be 16 bytes");
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
        GlowAdvancedParams params;
        params.radius = 0.0f;
        params.intensity = intensity;
        params.color_r = tint.r;
        params.color_g = tint.g;
        params.color_b = tint.b;
        params.color_a = tint.a;
        params.color_b_r = tint.r;
        params.color_b_g = tint.g;
        params.color_b_b = tint.b;
        params.color_b_a = tint.a;
        params.threshold = 0.0f;
        params.falloff = 1.0f;
        record_glow_prepare_advanced(command, descriptors, destination, source, params);
    }

    void VulkanBackend::Impl::record_glow_prepare_advanced(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        const GlowAdvancedParams& params) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::GlowPrepare)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float color_a[4];
            float color_b[4];
            float threshold;
            float intensity;
            float padding[2];
        } push{{params.color_r, params.color_g, params.color_b, params.color_a},
               {params.color_b_r, params.color_b_g, params.color_b_b, params.color_b_a},
               params.threshold, params.intensity, {0.0f, 0.0f}};
        static_assert(sizeof(push) == 48, "Glow prepare push must be 48 bytes");
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
            std::int32_t clip_bounds[4];
        } params{};
        static_assert(sizeof(PushConstants) == 112);
        for (int row = 0; row < 5; ++row)
            for (int lane = 0; lane < 4; ++lane)
                params.p[row][lane] = transform.p[static_cast<std::size_t>(row)][static_cast<std::size_t>(lane)];
        params.options[0] = static_cast<float>(transform.mode);
        params.options[1] = transform.clamp_unit ? 1.0f : 0.0f;
        params.options[2] = transform.straight_alpha ? 1.0f : 0.0f;
        params.options[3] = transform.clip_enabled ? 1.0f : 0.0f;
        for (int lane = 0; lane < 4; ++lane)
            params.clip_bounds[lane] = transform.clip_bounds[static_cast<std::size_t>(lane)];
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
        const Color& multiplier, Vec3 normal, float layer_z,
        int origin_x, int origin_y, float diffuse,
        std::uint32_t light_count, VkBuffer light_buffer,
        VkDeviceSize light_buffer_bytes) {
        const VkDescriptorBufferInfo light_info{light_buffer, 0, light_buffer_bytes};
        const VkWriteDescriptorSet light_write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 3, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &light_info, nullptr};
        vkUpdateDescriptorSets(device, 1, &light_write, 0, nullptr);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Lighting)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float multiplier[4];
            float normal_z[4];
            float origin_diffuse_count[4];
        } params{{multiplier.r, multiplier.g, multiplier.b, multiplier.a},
                 {normal.x, normal.y, normal.z, layer_z},
                 {static_cast<float>(origin_x), static_cast<float>(origin_y),
                  diffuse, static_cast<float>(light_count)}};
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        const VkBufferMemoryBarrier2KHR light_upload_barrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
            VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
            VK_ACCESS_2_SHADER_READ_BIT_KHR,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            light_buffer, 0, light_buffer_bytes};
        emit_buffer_barriers2(command,
            std::span<const VkBufferMemoryBarrier2KHR>{&light_upload_barrier, 1});
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }
} // namespace chronon3d::backends::vulkan
