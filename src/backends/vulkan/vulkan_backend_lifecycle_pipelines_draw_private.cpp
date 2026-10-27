// vulkan_backend_lifecycle_pipelines_draw_private.cpp — text and fill/layer compute kernel pipelines.
// Split from vulkan_backend_lifecycle_private.cpp per issue #51;
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"
#include "text_run_comp_spv.hpp"
#include "fill_rect_comp_spv.hpp"
#include "layer_batch_comp_spv.hpp"
#include "text_tile_bin_comp_spv.hpp"
#include "text_tile_raster_comp_spv.hpp"


namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::init_draw_kernel_pipelines() {
        const VkShaderModuleCreateInfo text_run_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_text_run_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_text_run_comp_spv)};
        VkShaderModule text_run_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &text_run_shader_info, nullptr, &text_run_shader),
              "vkCreateShaderModule(text run)");
        const VkPipelineShaderStageCreateInfo text_run_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            text_run_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_run_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_run_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_run_pipeline = VK_NULL_HANDLE;
        const VkResult text_run_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &text_run_pipeline_info, nullptr, &text_run_pipeline);
        vkDestroyShaderModule(device, text_run_shader, nullptr);
        check(text_run_result, "vkCreateComputePipelines(text run)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::TextRun, text_run_pipeline, "TextRun");
        if (debug_context) debug_context->set_pipeline_name(text_run_pipeline, "Chronon3D.Pipeline.TextRun");

        const VkShaderModuleCreateInfo fill_rect_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_fill_rect_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_fill_rect_comp_spv)};
        VkShaderModule fill_rect_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &fill_rect_shader_info, nullptr, &fill_rect_shader),
              "vkCreateShaderModule(fill rect)");
        const VkPipelineShaderStageCreateInfo fill_rect_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            fill_rect_shader, "main", nullptr};
        const VkComputePipelineCreateInfo fill_rect_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            fill_rect_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline fill_rect_pipeline = VK_NULL_HANDLE;
        const VkResult fill_rect_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &fill_rect_pipeline_info, nullptr, &fill_rect_pipeline);
        vkDestroyShaderModule(device, fill_rect_shader, nullptr);
        check(fill_rect_result, "vkCreateComputePipelines(fill rect)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::FillRect, fill_rect_pipeline, "FillRect");
        if (debug_context) debug_context->set_pipeline_name(fill_rect_pipeline, "Chronon3D.Pipeline.FillRect");

        const VkShaderModuleCreateInfo layer_batch_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_layer_batch_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_layer_batch_comp_spv)};
        VkShaderModule layer_batch_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &layer_batch_shader_info, nullptr, &layer_batch_shader),
              "vkCreateShaderModule(layer batch)");
        const VkPipelineShaderStageCreateInfo layer_batch_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            layer_batch_shader, "main", nullptr};
        const VkComputePipelineCreateInfo layer_batch_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            layer_batch_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline layer_batch_pipeline = VK_NULL_HANDLE;
        const VkResult layer_batch_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &layer_batch_pipeline_info, nullptr, &layer_batch_pipeline);
        vkDestroyShaderModule(device, layer_batch_shader, nullptr);
        check(layer_batch_result, "vkCreateComputePipelines(layer batch)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::LayerBatch, layer_batch_pipeline, "LayerBatch");
        if (debug_context) debug_context->set_pipeline_name(layer_batch_pipeline, "Chronon3D.Pipeline.LayerBatch");

        const VkShaderModuleCreateInfo text_tile_bin_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_text_tile_bin_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_text_tile_bin_comp_spv)};
        VkShaderModule text_tile_bin_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &text_tile_bin_shader_info, nullptr,
                                   &text_tile_bin_shader),
              "vkCreateShaderModule(text tile bin)");
        const VkPipelineShaderStageCreateInfo text_tile_bin_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            text_tile_bin_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_tile_bin_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_tile_bin_stage, kernels.text_tile_bin_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_tile_bin_pipeline = VK_NULL_HANDLE;
        const VkResult text_tile_bin_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &text_tile_bin_pipeline_info, nullptr,
            &text_tile_bin_pipeline);
        vkDestroyShaderModule(device, text_tile_bin_shader, nullptr);
        check(text_tile_bin_result, "vkCreateComputePipelines(text tile bin)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::TextTileBin, text_tile_bin_pipeline, "TextTileBin");
        if (debug_context) debug_context->set_pipeline_name(text_tile_bin_pipeline, "Chronon3D.Pipeline.TextTileBin");

        const VkShaderModuleCreateInfo text_tile_raster_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_text_tile_raster_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_text_tile_raster_comp_spv)};
        VkShaderModule text_tile_raster_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &text_tile_raster_shader_info, nullptr,
                                   &text_tile_raster_shader),
              "vkCreateShaderModule(text tile raster)");
        const VkPipelineShaderStageCreateInfo text_tile_raster_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            0, VK_SHADER_STAGE_COMPUTE_BIT,
            text_tile_raster_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_tile_raster_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_tile_raster_stage, kernels.text_tile_raster_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_tile_raster_pipeline = VK_NULL_HANDLE;
        const VkResult text_tile_raster_result = vkCreateComputePipelines(
            device, kernels.pipeline_cache, 1, &text_tile_raster_pipeline_info, nullptr,
            &text_tile_raster_pipeline);
        vkDestroyShaderModule(device, text_tile_raster_shader, nullptr);
        check(text_tile_raster_result, "vkCreateComputePipelines(text tile raster)");
        register_kernel_or_throw(kernels.registry, device,
                GpuKernelId::TextTileRaster, text_tile_raster_pipeline, "TextTileRaster");
        if (debug_context) debug_context->set_pipeline_name(text_tile_raster_pipeline, "Chronon3D.Pipeline.TextTileRaster");


}

} // namespace chronon3d::backends::vulkan
