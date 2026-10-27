// vulkan_kernel_record_composite_private.cpp — composite, transform and fill/matte kernel recording.
// Split from the oversized unit per issue #51 (LEANUP-001);
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

// record_composite split per issue #51 into geometry → material →
// descriptors → submit. Byte layout and math are unchanged: the phases only
// factor the std140 push-constant contract into named steps.
namespace {

struct CompositeGeometry {
    float clip_rect[4];
    std::int32_t dispatch_x0;
    std::int32_t dispatch_y0;
    std::int32_t dispatch_x1;
    std::int32_t dispatch_y1;
};

CompositeGeometry composite_geometry(
        const VulkanBackend::Impl::Image& destination,
        const std::optional<raster::BBox>& clip) {
    CompositeGeometry geometry{
        {0.0f, 0.0f, static_cast<float>(destination.width),
         static_cast<float>(destination.height)}, 0, 0, 0, 0};
    if (clip) {
        geometry.clip_rect[0] = static_cast<float>(std::max(0, clip->x0));
        geometry.clip_rect[1] = static_cast<float>(std::max(0, clip->y0));
        geometry.clip_rect[2] = static_cast<float>(std::min(
            static_cast<std::int32_t>(destination.width), clip->x1));
        geometry.clip_rect[3] = static_cast<float>(std::min(
            static_cast<std::int32_t>(destination.height), clip->y1));
    }
    geometry.dispatch_x0 = std::clamp(
        static_cast<std::int32_t>(geometry.clip_rect[0]), 0,
        static_cast<std::int32_t>(destination.width));
    geometry.dispatch_y0 = std::clamp(
        static_cast<std::int32_t>(geometry.clip_rect[1]), 0,
        static_cast<std::int32_t>(destination.height));
    geometry.dispatch_x1 = std::clamp(
        static_cast<std::int32_t>(geometry.clip_rect[2]), geometry.dispatch_x0,
        static_cast<std::int32_t>(destination.width));
    geometry.dispatch_y1 = std::clamp(
        static_cast<std::int32_t>(geometry.clip_rect[3]), geometry.dispatch_y0,
        static_cast<std::int32_t>(destination.height));
    return geometry;
}

// GLSL std140 push-constant alignment places the vec4 tint at
// byte 16 and the clip rectangle at byte 32.
struct CompositePushConstants {
    std::int32_t blend_mode;
    float source_scale;
    float padding[2];
    float tint[4];
    float clip_rect[4];
    std::int32_t dispatch_origin[2];
    std::int32_t premultiplied_source_over;
    std::int32_t push_padding;
};

CompositePushConstants composite_material(
        std::int32_t blend_mode, float source_scale, const float tint[4],
        bool premultiplied_source_over, const CompositeGeometry& geometry) {
    return CompositePushConstants{
        blend_mode, source_scale, {0.0f, 0.0f},
        {tint[0], tint[1], tint[2], tint[3]},
        {geometry.clip_rect[0], geometry.clip_rect[1],
         geometry.clip_rect[2], geometry.clip_rect[3]},
        {geometry.dispatch_x0, geometry.dispatch_y0},
        premultiplied_source_over ? 1 : 0, 0};
}

void composite_descriptors(VkCommandBuffer command, VkDescriptorSet descriptors,
                           VkPipelineLayout layout, VkPipeline pipeline) {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            layout, 0, 1, &descriptors, 0, nullptr);
}

void composite_submit(VkCommandBuffer command, VkPipelineLayout layout,
                      const CompositePushConstants& params,
                      const CompositeGeometry& geometry) {
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(params), &params);
    vkCmdDispatch(command, (geometry.dispatch_x1 - geometry.dispatch_x0 + 15) / 16,
                  (geometry.dispatch_y1 - geometry.dispatch_y0 + 15) / 16, 1);
}

} // namespace

void VulkanBackend::Impl::record_composite(
        VkCommandBuffer command, VkDescriptorSet descriptors,
        const Image& destination, const Image& source,
        std::int32_t blend_mode, float source_scale,
        const float tint[4],
        const std::optional<raster::BBox>& clip,
        bool premultiplied_source_over) {
    const CompositeGeometry geometry = composite_geometry(destination, clip);
    const CompositePushConstants params = composite_material(
        blend_mode, source_scale, tint, premultiplied_source_over, geometry);
    composite_descriptors(
        command, descriptors, kernels.general_layout,
        reinterpret_cast<VkPipeline>(
            kernels.registry.resolve(GpuKernelId::Composite)));
    composite_submit(command, kernels.general_layout, params, geometry);
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
} // namespace chronon3d::backends::vulkan
