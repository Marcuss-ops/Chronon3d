// vulkan_backend_lifecycle_pipelines_private.cpp — compute kernel pipelines (filter/color/draw family).
// Split from vulkan_backend_lifecycle_private.cpp per issue #51;
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"
#include "composite_comp_spv.hpp"
#include "transform_comp_spv.hpp"
#include "affine_transform_comp_spv.hpp"
#include "blur_comp_spv.hpp"
#include "directional_blur_comp_spv.hpp"
#include "radial_blur_comp_spv.hpp"
#include "dof_depth_stamp_comp_spv.hpp"
#include "dof_blur_comp_spv.hpp"
#include "warp_bulk_comp_spv.hpp"
#include "generate_bulk_comp_spv.hpp"
#include "lut_bulk_comp_spv.hpp"
#include "sharpen_comp_spv.hpp"
#include "bloom_threshold_comp_spv.hpp"
#include "glow_prepare_comp_spv.hpp"
#include "color_adjust_comp_spv.hpp"
#include "color_matrix_comp_spv.hpp"
#include "chromatic_aberration_comp_spv.hpp"
#include "lighting_comp_spv.hpp"
#include "matte_comp_spv.hpp"


namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::init_core_kernel_pipelines() {
        const VkShaderModuleCreateInfo shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_composite_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_composite_comp_spv)};
        VkShaderModule shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &shader_info, nullptr, &shader),
              "vkCreateShaderModule");
        const VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            shader, "main", nullptr};
        const VkComputePipelineCreateInfo pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline composite_pipeline = VK_NULL_HANDLE;
        const VkResult pipeline_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &pipeline_info, nullptr, &composite_pipeline);
        vkDestroyShaderModule(device, shader, nullptr);
        check(pipeline_result, "vkCreateComputePipelines");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::Composite, composite_pipeline, "Composite");
        if (debug_context) debug_context->set_pipeline_name(composite_pipeline, "Chronon3D.Pipeline.Composite");

        const VkShaderModuleCreateInfo transform_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_transform_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_transform_comp_spv)};
        VkShaderModule transform_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &transform_shader_info, nullptr, &transform_shader),
              "vkCreateShaderModule(transform)");
        const VkPipelineShaderStageCreateInfo transform_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            transform_shader, "main", nullptr};
        const VkComputePipelineCreateInfo transform_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            transform_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline transform_pipeline = VK_NULL_HANDLE;
        const VkResult transform_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &transform_pipeline_info, nullptr, &transform_pipeline);
        vkDestroyShaderModule(device, transform_shader, nullptr);
        check(transform_result, "vkCreateComputePipelines(transform)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::Transform, transform_pipeline, "Transform");
        if (debug_context) debug_context->set_pipeline_name(transform_pipeline, "Chronon3D.Pipeline.Transform");

        const VkShaderModuleCreateInfo affine_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_affine_transform_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_affine_transform_comp_spv)};
        VkShaderModule affine_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &affine_shader_info, nullptr, &affine_shader),
              "vkCreateShaderModule(affine transform)");
        const VkPipelineShaderStageCreateInfo affine_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            affine_shader, "main", nullptr};
        const VkComputePipelineCreateInfo affine_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            affine_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline affine_transform_pipeline = VK_NULL_HANDLE;
        const VkResult affine_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &affine_pipeline_info, nullptr, &affine_transform_pipeline);
        vkDestroyShaderModule(device, affine_shader, nullptr);
        check(affine_result, "vkCreateComputePipelines(affine transform)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::AffineTransform, affine_transform_pipeline, "AffineTransform");
        if (debug_context) debug_context->set_pipeline_name(affine_transform_pipeline, "Chronon3D.Pipeline.AffineTransform");

        const VkShaderModuleCreateInfo blur_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_blur_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_blur_comp_spv)};
        VkShaderModule blur_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &blur_shader_info, nullptr, &blur_shader),
              "vkCreateShaderModule(blur)");
        const VkPipelineShaderStageCreateInfo blur_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            blur_shader, "main", nullptr};
        const VkComputePipelineCreateInfo blur_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            blur_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline blur_pipeline = VK_NULL_HANDLE;
        const VkResult blur_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &blur_pipeline_info, nullptr, &blur_pipeline);
        vkDestroyShaderModule(device, blur_shader, nullptr);
        check(blur_result, "vkCreateComputePipelines(blur)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::Blur, blur_pipeline, "Blur");
        if (debug_context) debug_context->set_pipeline_name(blur_pipeline, "Chronon3D.Pipeline.Blur");

        const VkShaderModuleCreateInfo directional_blur_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_directional_blur_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_directional_blur_comp_spv)};
        VkShaderModule directional_blur_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &directional_blur_shader_info, nullptr, &directional_blur_shader),
              "vkCreateShaderModule(directional_blur)");
        const VkPipelineShaderStageCreateInfo directional_blur_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            directional_blur_shader, "main", nullptr};
        const VkComputePipelineCreateInfo directional_blur_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            directional_blur_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline directional_blur_pipeline = VK_NULL_HANDLE;
        const VkResult directional_blur_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &directional_blur_pipeline_info, nullptr, &directional_blur_pipeline);
        vkDestroyShaderModule(device, directional_blur_shader, nullptr);
        check(directional_blur_result, "vkCreateComputePipelines(directional_blur)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::DirectionalBlur, directional_blur_pipeline, "DirectionalBlur");
        if (debug_context) debug_context->set_pipeline_name(directional_blur_pipeline, "Chronon3D.Pipeline.DirectionalBlur");

        const VkShaderModuleCreateInfo radial_blur_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_radial_blur_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_radial_blur_comp_spv)};
        VkShaderModule radial_blur_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &radial_blur_shader_info, nullptr, &radial_blur_shader),
              "vkCreateShaderModule(radial_blur)");
        const VkPipelineShaderStageCreateInfo radial_blur_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            radial_blur_shader, "main", nullptr};
        const VkComputePipelineCreateInfo radial_blur_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            radial_blur_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline radial_blur_pipeline = VK_NULL_HANDLE;
        const VkResult radial_blur_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &radial_blur_pipeline_info, nullptr, &radial_blur_pipeline);
        vkDestroyShaderModule(device, radial_blur_shader, nullptr);
        check(radial_blur_result, "vkCreateComputePipelines(radial_blur)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::RadialBlur, radial_blur_pipeline, "RadialBlur");
        if (debug_context) debug_context->set_pipeline_name(radial_blur_pipeline, "Chronon3D.Pipeline.RadialBlur");

        const VkShaderModuleCreateInfo dof_depth_stamp_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_dof_depth_stamp_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_dof_depth_stamp_comp_spv)};
        VkShaderModule dof_depth_stamp_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &dof_depth_stamp_shader_info, nullptr, &dof_depth_stamp_shader),
              "vkCreateShaderModule(dof_depth_stamp)");
        const VkPipelineShaderStageCreateInfo dof_depth_stamp_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            dof_depth_stamp_shader, "main", nullptr};
        const VkComputePipelineCreateInfo dof_depth_stamp_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            dof_depth_stamp_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline dof_depth_stamp_pipeline = VK_NULL_HANDLE;
        const VkResult dof_depth_stamp_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &dof_depth_stamp_pipeline_info, nullptr, &dof_depth_stamp_pipeline);
        vkDestroyShaderModule(device, dof_depth_stamp_shader, nullptr);
        check(dof_depth_stamp_result, "vkCreateComputePipelines(dof_depth_stamp)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::DofDepthStamp, dof_depth_stamp_pipeline, "DofDepthStamp");
        if (debug_context) debug_context->set_pipeline_name(dof_depth_stamp_pipeline, "Chronon3D.Pipeline.DofDepthStamp");

        const VkShaderModuleCreateInfo dof_blur_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_dof_blur_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_dof_blur_comp_spv)};
        VkShaderModule dof_blur_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &dof_blur_shader_info, nullptr, &dof_blur_shader),
              "vkCreateShaderModule(dof_blur)");
        const VkPipelineShaderStageCreateInfo dof_blur_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            dof_blur_shader, "main", nullptr};
        const VkComputePipelineCreateInfo dof_blur_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            dof_blur_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline dof_blur_pipeline = VK_NULL_HANDLE;
        const VkResult dof_blur_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &dof_blur_pipeline_info, nullptr, &dof_blur_pipeline);
        vkDestroyShaderModule(device, dof_blur_shader, nullptr);
        check(dof_blur_result, "vkCreateComputePipelines(dof_blur)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::DofBlur, dof_blur_pipeline, "DofBlur");
        if (debug_context) debug_context->set_pipeline_name(dof_blur_pipeline, "Chronon3D.Pipeline.DofBlur");

        const VkShaderModuleCreateInfo warp_bulk_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_warp_bulk_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_warp_bulk_comp_spv)};
        VkShaderModule warp_bulk_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &warp_bulk_shader_info, nullptr, &warp_bulk_shader),
              "vkCreateShaderModule(warp_bulk)");
        const VkPipelineShaderStageCreateInfo warp_bulk_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            warp_bulk_shader, "main", nullptr};
        const VkComputePipelineCreateInfo warp_bulk_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            warp_bulk_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline warp_bulk_pipeline = VK_NULL_HANDLE;
        const VkResult warp_bulk_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &warp_bulk_pipeline_info, nullptr, &warp_bulk_pipeline);
        vkDestroyShaderModule(device, warp_bulk_shader, nullptr);
        check(warp_bulk_result, "vkCreateComputePipelines(warp_bulk)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::WarpBulk, warp_bulk_pipeline, "WarpBulk");
        if (debug_context) debug_context->set_pipeline_name(warp_bulk_pipeline, "Chronon3D.Pipeline.WarpBulk");

        const VkShaderModuleCreateInfo generate_bulk_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_generate_bulk_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_generate_bulk_comp_spv)};
        VkShaderModule generate_bulk_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &generate_bulk_shader_info, nullptr, &generate_bulk_shader),
              "vkCreateShaderModule(generate_bulk)");
        const VkPipelineShaderStageCreateInfo generate_bulk_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            generate_bulk_shader, "main", nullptr};
        const VkComputePipelineCreateInfo generate_bulk_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            generate_bulk_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline generate_bulk_pipeline = VK_NULL_HANDLE;
        const VkResult generate_bulk_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &generate_bulk_pipeline_info, nullptr, &generate_bulk_pipeline);
        vkDestroyShaderModule(device, generate_bulk_shader, nullptr);
        check(generate_bulk_result, "vkCreateComputePipelines(generate_bulk)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::GenerateBulk, generate_bulk_pipeline, "GenerateBulk");
        if (debug_context) debug_context->set_pipeline_name(generate_bulk_pipeline, "Chronon3D.Pipeline.GenerateBulk");

        const VkShaderModuleCreateInfo lut_bulk_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_lut_bulk_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_lut_bulk_comp_spv)};
        VkShaderModule lut_bulk_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &lut_bulk_shader_info, nullptr, &lut_bulk_shader),
              "vkCreateShaderModule(lut_bulk)");
        const VkPipelineShaderStageCreateInfo lut_bulk_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            lut_bulk_shader, "main", nullptr};
        const VkComputePipelineCreateInfo lut_bulk_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            lut_bulk_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline lut_bulk_pipeline = VK_NULL_HANDLE;
        const VkResult lut_bulk_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &lut_bulk_pipeline_info, nullptr, &lut_bulk_pipeline);
        vkDestroyShaderModule(device, lut_bulk_shader, nullptr);
        check(lut_bulk_result, "vkCreateComputePipelines(lut_bulk)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::LutBulk, lut_bulk_pipeline, "LutBulk");
        if (debug_context) debug_context->set_pipeline_name(lut_bulk_pipeline, "Chronon3D.Pipeline.LutBulk");

        const VkShaderModuleCreateInfo sharpen_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_sharpen_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_sharpen_comp_spv)};
        VkShaderModule sharpen_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &sharpen_shader_info, nullptr, &sharpen_shader),
              "vkCreateShaderModule(sharpen)");
        const VkPipelineShaderStageCreateInfo sharpen_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            sharpen_shader, "main", nullptr};
        const VkComputePipelineCreateInfo sharpen_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            sharpen_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline sharpen_pipeline = VK_NULL_HANDLE;
        const VkResult sharpen_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &sharpen_pipeline_info, nullptr, &sharpen_pipeline);
        vkDestroyShaderModule(device, sharpen_shader, nullptr);
        check(sharpen_result, "vkCreateComputePipelines(sharpen)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::Sharpen, sharpen_pipeline, "Sharpen");
        if (debug_context) debug_context->set_pipeline_name(sharpen_pipeline, "Chronon3D.Pipeline.Sharpen");

        const VkShaderModuleCreateInfo bloom_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_bloom_threshold_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_bloom_threshold_comp_spv)};
        VkShaderModule bloom_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &bloom_shader_info, nullptr, &bloom_shader),
              "vkCreateShaderModule(bloom threshold)");
        const VkPipelineShaderStageCreateInfo bloom_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT, bloom_shader, "main", nullptr};
        const VkComputePipelineCreateInfo bloom_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            bloom_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline bloom_pipeline = VK_NULL_HANDLE;
        const VkResult bloom_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &bloom_pipeline_info, nullptr, &bloom_pipeline);
        vkDestroyShaderModule(device, bloom_shader, nullptr);
        check(bloom_result, "vkCreateComputePipelines(bloom threshold)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::BloomThreshold, bloom_pipeline, "BloomThreshold");
        if (debug_context) debug_context->set_pipeline_name(bloom_pipeline, "Chronon3D.Pipeline.BloomThreshold");

        const VkShaderModuleCreateInfo glow_prepare_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_glow_prepare_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_glow_prepare_comp_spv)};
        VkShaderModule glow_prepare_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &glow_prepare_shader_info, nullptr,
                                  &glow_prepare_shader),
              "vkCreateShaderModule(glow prepare)");
        const VkPipelineShaderStageCreateInfo glow_prepare_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_COMPUTE_BIT, glow_prepare_shader, "main", nullptr};
        const VkComputePipelineCreateInfo glow_prepare_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            glow_prepare_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline glow_prepare_pipeline = VK_NULL_HANDLE;
        const VkResult glow_prepare_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &glow_prepare_pipeline_info, nullptr,
            &glow_prepare_pipeline);
        vkDestroyShaderModule(device, glow_prepare_shader, nullptr);
        check(glow_prepare_result, "vkCreateComputePipelines(glow prepare)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::GlowPrepare, glow_prepare_pipeline, "GlowPrepare");
        if (debug_context)
            debug_context->set_pipeline_name(glow_prepare_pipeline,
                                             "Chronon3D.Pipeline.GlowPrepare");

        const VkShaderModuleCreateInfo color_adjust_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_color_adjust_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_color_adjust_comp_spv)};
        VkShaderModule color_adjust_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &color_adjust_shader_info, nullptr, &color_adjust_shader),
              "vkCreateShaderModule(color adjust)");
        const VkPipelineShaderStageCreateInfo color_adjust_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            color_adjust_shader, "main", nullptr};
        const VkComputePipelineCreateInfo color_adjust_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            color_adjust_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline color_adjust_pipeline = VK_NULL_HANDLE;
        const VkResult color_adjust_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &color_adjust_pipeline_info, nullptr, &color_adjust_pipeline);
        vkDestroyShaderModule(device, color_adjust_shader, nullptr);
        check(color_adjust_result, "vkCreateComputePipelines(color adjust)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::ColorAdjust, color_adjust_pipeline, "ColorAdjust");
        if (debug_context) debug_context->set_pipeline_name(color_adjust_pipeline, "Chronon3D.Pipeline.ColorAdjust");

        const VkShaderModuleCreateInfo color_matrix_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_color_matrix_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_color_matrix_comp_spv)};
        VkShaderModule color_matrix_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &color_matrix_shader_info, nullptr, &color_matrix_shader),
              "vkCreateShaderModule(color matrix)");
        const VkPipelineShaderStageCreateInfo color_matrix_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            color_matrix_shader, "main", nullptr};
        const VkComputePipelineCreateInfo color_matrix_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            color_matrix_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline color_matrix_pipeline = VK_NULL_HANDLE;
        const VkResult color_matrix_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &color_matrix_pipeline_info, nullptr, &color_matrix_pipeline);
        vkDestroyShaderModule(device, color_matrix_shader, nullptr);
        check(color_matrix_result, "vkCreateComputePipelines(color matrix)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::ColorMatrix, color_matrix_pipeline, "ColorMatrix");
        if (debug_context) debug_context->set_pipeline_name(color_matrix_pipeline, "Chronon3D.Pipeline.ColorMatrix");

        const VkShaderModuleCreateInfo chromatic_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_chromatic_aberration_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_chromatic_aberration_comp_spv)};
        VkShaderModule chromatic_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &chromatic_shader_info, nullptr, &chromatic_shader),
              "vkCreateShaderModule(chromatic aberration)");
        const VkPipelineShaderStageCreateInfo chromatic_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT, chromatic_shader, "main", nullptr};
        const VkComputePipelineCreateInfo chromatic_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            chromatic_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline chromatic_pipeline = VK_NULL_HANDLE;
        const VkResult chromatic_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &chromatic_pipeline_info, nullptr, &chromatic_pipeline);
        vkDestroyShaderModule(device, chromatic_shader, nullptr);
        check(chromatic_result, "vkCreateComputePipelines(chromatic aberration)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::ChromaticAberration, chromatic_pipeline, "ChromaticAberration");
        if (debug_context) debug_context->set_pipeline_name(
            chromatic_pipeline, "Chronon3D.Pipeline.ChromaticAberration");

        const VkShaderModuleCreateInfo lighting_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_lighting_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_lighting_comp_spv)};
        VkShaderModule lighting_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &lighting_shader_info, nullptr, &lighting_shader),
              "vkCreateShaderModule(lighting)");
        const VkPipelineShaderStageCreateInfo lighting_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            lighting_shader, "main", nullptr};
        const VkComputePipelineCreateInfo lighting_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            lighting_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline lighting_pipeline = VK_NULL_HANDLE;
        const VkResult lighting_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &lighting_pipeline_info, nullptr, &lighting_pipeline);
        vkDestroyShaderModule(device, lighting_shader, nullptr);
        check(lighting_result, "vkCreateComputePipelines(lighting)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::Lighting, lighting_pipeline, "Lighting");
        if (debug_context) debug_context->set_pipeline_name(lighting_pipeline, "Chronon3D.Pipeline.Lighting");

        const VkShaderModuleCreateInfo matte_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_matte_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_matte_comp_spv)};
        VkShaderModule matte_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &matte_shader_info, nullptr, &matte_shader),
              "vkCreateShaderModule(matte)");
        const VkPipelineShaderStageCreateInfo matte_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            matte_shader, "main", nullptr};
        const VkComputePipelineCreateInfo matte_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            matte_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline matte_pipeline = VK_NULL_HANDLE;
        const VkResult matte_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &matte_pipeline_info, nullptr, &matte_pipeline);
        vkDestroyShaderModule(device, matte_shader, nullptr);
        check(matte_result, "vkCreateComputePipelines(matte)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::Matte, matte_pipeline, "Matte");
        if (debug_context) debug_context->set_pipeline_name(matte_pipeline, "Chronon3D.Pipeline.Matte");


}

} // namespace chronon3d::backends::vulkan
