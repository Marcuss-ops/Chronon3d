// vulkan_backend_lifecycle_private.cpp — VulkanBackend::Impl construction
// and destruction. Out-of-class definitions; declared in
// vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"
#include "composite_comp_spv.hpp"
#include "transform_comp_spv.hpp"
#include "affine_transform_comp_spv.hpp"
#include "blur_comp_spv.hpp"
#include "color_adjust_comp_spv.hpp"
#include "matte_comp_spv.hpp"
#include "text_run_comp_spv.hpp"
#include "fill_rect_comp_spv.hpp"
#include "layer_batch_comp_spv.hpp"
#include "text_batch_comp_spv.hpp"
#include "text_tile_bin_comp_spv.hpp"
#include "text_tile_raster_comp_spv.hpp"

namespace chronon3d::backends::vulkan {

    VulkanBackend::Impl::Impl(VkInstance inst, VkPhysicalDevice physical, VkDevice logical, VkQueue graphics,
         std::uint32_t family, VkCommandPool pool,
         bool calibrated_timestamps_supported,
         VulkanDebugContext* dbg_ctx)
        : instance(inst), physical_device(physical), device(logical), queue(graphics),
          queue_family(family), command_pool(pool),
          submission_ring(descriptor_arena),
          frame_batch(submission_ring),
          calibrated_ts_supported(calibrated_timestamps_supported),
          debug_context(dbg_ctx) {
        surfaces.owner_ = this;
        memory_manager.initialize(instance, physical_device, device);
        VkPhysicalDeviceProperties device_properties{};
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);
        stats.device_name = device_properties.deviceName;
        stats.discrete_gpu =
            device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        timestamp_period_ns = device_properties.limits.timestampPeriod;
        // timestampValidBits is a property of the selected queue family, not
        // VkPhysicalDeviceLimits.  Read it from the same family used for the
        // graphics queue so timestamp queries are enabled only when that
        // queue can actually report them.
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
        if (queue_family < families.size()) {
            timestamp_valid_bits = families[queue_family].timestampValidBits;
        }
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        const VkDescriptorSetLayoutCreateInfo layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 5, bindings};
        check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_layout),
              "vkCreateDescriptorSetLayout");
        if (debug_context) debug_context->set_descriptor_set_layout_name(descriptor_layout, "Chronon3D.DescriptorSetLayout.General");

        // One persistent set serves the single-pass operations; glow reuses
        // three additional sets so all three dispatches in its one command
        // buffer retain distinct image bindings until execution.  Frame
        // batches allocate one descriptor set per recorded pass from the
        // current ring slot's own allocator (see FrameBatchState), so the
        // shared pool only ever serves the persistent standalone sets and
        // never needs to be reset on the frame boundary.
        const VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512}};
        const VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            256, 2, pool_sizes};
        check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool),
              "vkCreateDescriptorPool");
        if (debug_context) debug_context->set_descriptor_pool_name(descriptor_pool, "Chronon3D.DescriptorPool.General");

        const VkPushConstantRange push_constants{
            VK_SHADER_STAGE_COMPUTE_BIT, 0, 128};
        const VkPipelineLayoutCreateInfo pipeline_layout_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &descriptor_layout,
            1, &push_constants};
        check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &kernels.general_layout),
              "vkCreatePipelineLayout");
        if (debug_context) debug_context->set_pipeline_layout_name(kernels.general_layout, "Chronon3D.PipelineLayout.General");

        const VkDescriptorSetLayoutBinding tile_bin_bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        const VkDescriptorSetLayoutCreateInfo tile_bin_layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            4, tile_bin_bindings};
        check(vkCreateDescriptorSetLayout(device, &tile_bin_layout_info, nullptr,
                                          &text_tile_bin_descriptor_layout),
              "vkCreateDescriptorSetLayout(text tile bin)");
        if (debug_context) debug_context->set_descriptor_set_layout_name(text_tile_bin_descriptor_layout, "Chronon3D.DescriptorSetLayout.TextTileBin");

        const VkDescriptorSetLayoutBinding tile_raster_bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        const VkDescriptorSetLayoutCreateInfo tile_raster_layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            6, tile_raster_bindings};
        check(vkCreateDescriptorSetLayout(device, &tile_raster_layout_info, nullptr,
                                          &text_tile_raster_descriptor_layout),
              "vkCreateDescriptorSetLayout(text tile raster)");
        if (debug_context) debug_context->set_descriptor_set_layout_name(text_tile_raster_descriptor_layout, "Chronon3D.DescriptorSetLayout.TextTileRaster");

        const VkPushConstantRange tile_bin_push{
            VK_SHADER_STAGE_COMPUTE_BIT, 0, 24};
        const VkPipelineLayoutCreateInfo tile_bin_pipeline_layout_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
            &text_tile_bin_descriptor_layout, 1, &tile_bin_push};
        check(vkCreatePipelineLayout(device, &tile_bin_pipeline_layout_info, nullptr,
                                     &kernels.text_tile_bin_layout),
              "vkCreatePipelineLayout(text tile bin)");
        if (debug_context) debug_context->set_pipeline_layout_name(kernels.text_tile_bin_layout, "Chronon3D.PipelineLayout.TextTileBin");

        const VkPushConstantRange tile_raster_push{
            VK_SHADER_STAGE_COMPUTE_BIT, 0, 28};
        const VkPipelineLayoutCreateInfo tile_raster_pipeline_layout_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
            &text_tile_raster_descriptor_layout, 1, &tile_raster_push};
        check(vkCreatePipelineLayout(device, &tile_raster_pipeline_layout_info, nullptr,
                                     &kernels.text_tile_raster_layout),
              "vkCreatePipelineLayout(text tile raster)");
        if (debug_context) debug_context->set_pipeline_layout_name(kernels.text_tile_raster_layout, "Chronon3D.PipelineLayout.TextTileRaster");

        const VkShaderModuleCreateInfo shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_composite_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_composite_comp_spv)};
        VkShaderModule shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &shader_info, nullptr, &shader),
              "vkCreateShaderModule");
        const VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            shader, "main", nullptr};
        const VkComputePipelineCreateInfo pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline composite_pipeline = VK_NULL_HANDLE;
        const VkResult pipeline_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &composite_pipeline);
        vkDestroyShaderModule(device, shader, nullptr);
        check(pipeline_result, "vkCreateComputePipelines");
        if (!kernels.registry.register_kernel(
                GpuKernelId::Composite,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(composite_pipeline))) {
            vkDestroyPipeline(device, composite_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Composite pipeline");
        }
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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            transform_shader, "main", nullptr};
        const VkComputePipelineCreateInfo transform_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            transform_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline transform_pipeline = VK_NULL_HANDLE;
        const VkResult transform_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &transform_pipeline_info, nullptr, &transform_pipeline);
        vkDestroyShaderModule(device, transform_shader, nullptr);
        check(transform_result, "vkCreateComputePipelines(transform)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::Transform,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(transform_pipeline))) {
            vkDestroyPipeline(device, transform_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Transform pipeline");
        }
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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            affine_shader, "main", nullptr};
        const VkComputePipelineCreateInfo affine_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            affine_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline affine_transform_pipeline = VK_NULL_HANDLE;
        const VkResult affine_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &affine_pipeline_info, nullptr, &affine_transform_pipeline);
        vkDestroyShaderModule(device, affine_shader, nullptr);
        check(affine_result, "vkCreateComputePipelines(affine transform)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::AffineTransform,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(affine_transform_pipeline))) {
            vkDestroyPipeline(device, affine_transform_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected AffineTransform pipeline");
        }
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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            blur_shader, "main", nullptr};
        const VkComputePipelineCreateInfo blur_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            blur_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline blur_pipeline = VK_NULL_HANDLE;
        const VkResult blur_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &blur_pipeline_info, nullptr, &blur_pipeline);
        vkDestroyShaderModule(device, blur_shader, nullptr);
        check(blur_result, "vkCreateComputePipelines(blur)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::Blur,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(blur_pipeline))) {
            vkDestroyPipeline(device, blur_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Blur pipeline");
        }
        if (debug_context) debug_context->set_pipeline_name(blur_pipeline, "Chronon3D.Pipeline.Blur");

        const VkShaderModuleCreateInfo color_adjust_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_color_adjust_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_color_adjust_comp_spv)};
        VkShaderModule color_adjust_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &color_adjust_shader_info, nullptr, &color_adjust_shader),
              "vkCreateShaderModule(color adjust)");
        const VkPipelineShaderStageCreateInfo color_adjust_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            color_adjust_shader, "main", nullptr};
        const VkComputePipelineCreateInfo color_adjust_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            color_adjust_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline color_adjust_pipeline = VK_NULL_HANDLE;
        const VkResult color_adjust_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &color_adjust_pipeline_info, nullptr, &color_adjust_pipeline);
        vkDestroyShaderModule(device, color_adjust_shader, nullptr);
        check(color_adjust_result, "vkCreateComputePipelines(color adjust)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::ColorAdjust,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(color_adjust_pipeline))) {
            vkDestroyPipeline(device, color_adjust_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected ColorAdjust pipeline");
        }
        if (debug_context) debug_context->set_pipeline_name(color_adjust_pipeline, "Chronon3D.Pipeline.ColorAdjust");

        const VkShaderModuleCreateInfo matte_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_matte_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_matte_comp_spv)};
        VkShaderModule matte_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &matte_shader_info, nullptr, &matte_shader),
              "vkCreateShaderModule(matte)");
        const VkPipelineShaderStageCreateInfo matte_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            matte_shader, "main", nullptr};
        const VkComputePipelineCreateInfo matte_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            matte_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline matte_pipeline = VK_NULL_HANDLE;
        const VkResult matte_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &matte_pipeline_info, nullptr, &matte_pipeline);
        vkDestroyShaderModule(device, matte_shader, nullptr);
        check(matte_result, "vkCreateComputePipelines(matte)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::Matte,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(matte_pipeline))) {
            vkDestroyPipeline(device, matte_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Matte pipeline");
        }
        if (debug_context) debug_context->set_pipeline_name(matte_pipeline, "Chronon3D.Pipeline.Matte");

        const VkShaderModuleCreateInfo text_run_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_text_run_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_text_run_comp_spv)};
        VkShaderModule text_run_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &text_run_shader_info, nullptr, &text_run_shader),
              "vkCreateShaderModule(text run)");
        const VkPipelineShaderStageCreateInfo text_run_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            text_run_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_run_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_run_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_run_pipeline = VK_NULL_HANDLE;
        const VkResult text_run_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &text_run_pipeline_info, nullptr, &text_run_pipeline);
        vkDestroyShaderModule(device, text_run_shader, nullptr);
        check(text_run_result, "vkCreateComputePipelines(text run)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::TextRun,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(text_run_pipeline))) {
            vkDestroyPipeline(device, text_run_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected TextRun pipeline");
        }
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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            fill_rect_shader, "main", nullptr};
        const VkComputePipelineCreateInfo fill_rect_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            fill_rect_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline fill_rect_pipeline = VK_NULL_HANDLE;
        const VkResult fill_rect_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &fill_rect_pipeline_info, nullptr, &fill_rect_pipeline);
        vkDestroyShaderModule(device, fill_rect_shader, nullptr);
        check(fill_rect_result, "vkCreateComputePipelines(fill rect)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::FillRect,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(fill_rect_pipeline))) {
            vkDestroyPipeline(device, fill_rect_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected FillRect pipeline");
        }
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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            layer_batch_shader, "main", nullptr};
        const VkComputePipelineCreateInfo layer_batch_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            layer_batch_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline layer_batch_pipeline = VK_NULL_HANDLE;
        const VkResult layer_batch_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &layer_batch_pipeline_info, nullptr, &layer_batch_pipeline);
        vkDestroyShaderModule(device, layer_batch_shader, nullptr);
        check(layer_batch_result, "vkCreateComputePipelines(layer batch)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::LayerBatch,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(layer_batch_pipeline))) {
            vkDestroyPipeline(device, layer_batch_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected LayerBatch pipeline");
        }
        if (debug_context) debug_context->set_pipeline_name(layer_batch_pipeline, "Chronon3D.Pipeline.LayerBatch");

        const VkShaderModuleCreateInfo text_batch_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_text_batch_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_text_batch_comp_spv)};
        VkShaderModule text_batch_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &text_batch_shader_info, nullptr, &text_batch_shader),
              "vkCreateShaderModule(text batch)");
        const VkPipelineShaderStageCreateInfo text_batch_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            text_batch_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_batch_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_batch_stage, kernels.general_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_batch_pipeline = VK_NULL_HANDLE;
        const VkResult text_batch_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &text_batch_pipeline_info, nullptr, &text_batch_pipeline);
        vkDestroyShaderModule(device, text_batch_shader, nullptr);
        check(text_batch_result, "vkCreateComputePipelines(text batch)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::TextBatch,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(text_batch_pipeline))) {
            vkDestroyPipeline(device, text_batch_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected TextBatch pipeline");
        }
        if (debug_context) debug_context->set_pipeline_name(text_batch_pipeline, "Chronon3D.Pipeline.TextBatch");

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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            text_tile_bin_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_tile_bin_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_tile_bin_stage, kernels.text_tile_bin_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_tile_bin_pipeline = VK_NULL_HANDLE;
        const VkResult text_tile_bin_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &text_tile_bin_pipeline_info, nullptr,
            &text_tile_bin_pipeline);
        vkDestroyShaderModule(device, text_tile_bin_shader, nullptr);
        check(text_tile_bin_result, "vkCreateComputePipelines(text tile bin)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::TextTileBin,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(text_tile_bin_pipeline))) {
            vkDestroyPipeline(device, text_tile_bin_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected TextTileBin pipeline");
        }
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
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            text_tile_raster_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_tile_raster_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_tile_raster_stage, kernels.text_tile_raster_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_tile_raster_pipeline = VK_NULL_HANDLE;
        const VkResult text_tile_raster_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &text_tile_raster_pipeline_info, nullptr,
            &text_tile_raster_pipeline);
        vkDestroyShaderModule(device, text_tile_raster_shader, nullptr);
        check(text_tile_raster_result, "vkCreateComputePipelines(text tile raster)");
        if (!kernels.registry.register_kernel(
                GpuKernelId::TextTileRaster,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(text_tile_raster_pipeline))) {
            vkDestroyPipeline(device, text_tile_raster_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected TextTileRaster pipeline");
        }
        if (debug_context) debug_context->set_pipeline_name(text_tile_raster_pipeline, "Chronon3D.Pipeline.TextTileRaster");

        const VkCommandBufferAllocateInfo command_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        check(vkAllocateCommandBuffers(device, &command_info, &command_buffer),
              "vkAllocateCommandBuffers");
        if (debug_context) debug_context->set_command_buffer_name(command_buffer, "Chronon3D.CommandBuffer.Main");
        // Dedicated command buffers + fences + descriptor pools for the
        // frame-batch ring so batch recording never conflicts with the
        // standalone command buffer used by uploads, downloads and
        // single-pass operations, and each slot can be synchronized and its
        // descriptor pool reset independently of the others.
        const VkCommandBufferAllocateInfo batch_command_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        for (auto& slot_buffer : frame_batch.command_buffers) {
            check(vkAllocateCommandBuffers(device, &batch_command_info, &slot_buffer),
                  "vkAllocateCommandBuffers(frame batch slot)");
            if (debug_context) debug_context->set_command_buffer_name(slot_buffer, "Chronon3D.CommandBuffer.FrameBatchSlot");
        }
        const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
        for (auto& slot_fence : frame_batch.fences) {
            check(vkCreateFence(device, &fence_info, nullptr, &slot_fence),
                  "vkCreateFence(frame batch slot)");
            if (debug_context) debug_context->set_fence_name(slot_fence, "Chronon3D.Fence.FrameBatchSlot");
        }
        for (auto& allocator : descriptor_arena.pass) {
            allocator.create(device, descriptor_layout);
        }
        for (auto& allocator : descriptor_arena.text_tile_bin) {
            allocator.create(device, text_tile_bin_descriptor_layout, 0, 4);
        }
        for (auto& allocator : descriptor_arena.text_tile_raster) {
            allocator.create(device, text_tile_raster_descriptor_layout, 2, 4);
        }
        check(vkCreateFence(device, &fence_info, nullptr, &fence), "vkCreateFence");
        if (debug_context) debug_context->set_fence_name(fence, "Chronon3D.Fence.Main");
        const VkSemaphoreTypeCreateInfo timeline_type{
            VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr,
            VK_SEMAPHORE_TYPE_TIMELINE, 0};
        const VkSemaphoreCreateInfo timeline_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timeline_type, 0};
        check(vkCreateSemaphore(device, &timeline_info, nullptr, &timeline_semaphore),
              "vkCreateSemaphore(timeline)");
        if (debug_context) debug_context->set_semaphore_name(timeline_semaphore, "Chronon3D.Semaphore.Timeline");

        if (timestamp_valid_bits != 0) {
            // Each ring slot reserves one legacy frame [start,end] pair plus
            // one [start,end] pair for every compiled pass.
            const auto queries_per_slot = static_cast<std::uint32_t>(
                2 + 2 * FrameBatchState::kCompiledPassTimingCapacity);
            const VkQueryPoolCreateInfo query_pool_info{
                VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0,
                VK_QUERY_TYPE_TIMESTAMP,
                static_cast<std::uint32_t>(
                    FrameBatchState::kSlotCount * queries_per_slot), 0};
            check(vkCreateQueryPool(device, &query_pool_info, nullptr, &timestamp_pool),
                  "vkCreateQueryPool(timestamp)");
            if (debug_context) debug_context->set_query_pool_name(timestamp_pool, "Chronon3D.QueryPool.PassTiming");
        }

        // VK_EXT_calibrated_timestamps: sample (device GPU timestamp, Perfetto
        // trace-clock ns) back-to-back to anchor GPU work on the CPU timeline.
        // One anchor taken at backend construction; completed pass query
        // results are then converted with this single (G0, C0) pair.  When
        // the extension or timestamp queries are unavailable the flag stays
        // false and only CPU-side submit/fence-wait events are traced — no
        // fake GPU bars.
        if (calibrated_ts_supported && timestamp_valid_bits != 0) {
            pfn_get_calibrated_timestamps =
                reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
                    vkGetDeviceProcAddr(device,
                                        "vkGetCalibratedTimestampsEXT"));
            if (pfn_get_calibrated_timestamps) {
                VkCalibratedTimestampInfoEXT device_domain{
                    VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr,
                    VK_TIME_DOMAIN_DEVICE_EXT};
                std::uint64_t gpu_ts = 0;
                std::uint64_t max_deviation = 0;
                if (pfn_get_calibrated_timestamps(device, 1, &device_domain,
                                                  &gpu_ts, &max_deviation) ==
                    VK_SUCCESS) {
                    calibration_gpu_ts = gpu_ts;
                    calibration_max_deviation = max_deviation;
                    calibration_cpu_trace_ns = chronon3d::tracing::TraceTimeNs();
                    gpu_timestamps_calibrated = true;
                    chronon3d::tracing::RegisterVulkanQueueTrack();
                }
            }
        }
    }

VulkanBackend::Impl::~Impl() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        surfaces.destroy_all(*this);
        destroy_image(dst);
        destroy_image(src);
        if (staging.buffer != VK_NULL_HANDLE) memory_manager.destroy_buffer(staging);
        for (std::size_t i = 0; i < kGlyphInstanceRingSize; ++i) {
            if (glyph_instance_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(glyph_instance_buffers[i]);
            }
            if (layer_instance_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(layer_instance_buffers[i]);
            }
            if (text_run_dynamic_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_run_dynamic_buffers[i]);
            }
            if (text_tile_count_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_tile_count_buffers[i]);
            }
            if (text_tile_index_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_tile_index_buffers[i]);
            }
        }
        for (auto& slot : uploads.slots) destroy_upload_slot(slot);
        descriptor_arena.destroy();
        for (auto& slot_fence : frame_batch.fences) {
            if (slot_fence != VK_NULL_HANDLE) vkDestroyFence(device, slot_fence, nullptr);
        }
        for (auto& slot_buffer : frame_batch.command_buffers) {
            if (slot_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot_buffer);
            }
        }
        // ── Replay slot teardown ───────────────────────────────────
        for (auto& slot : replay_slots) {
            if (slot.command_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
            }
            if (slot.fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, slot.fence, nullptr);
            }
            if (slot.params.buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(slot.params);
            }
        }
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (timeline_semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline_semaphore, nullptr);
        kernels.destroy(device);
        // Descriptor pools are destroyed by descriptor_arena.destroy().
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (text_tile_bin_descriptor_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_tile_bin_descriptor_layout, nullptr);
        }
        if (text_tile_raster_descriptor_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_tile_raster_descriptor_layout, nullptr);
        }
        if (timestamp_pool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestamp_pool, nullptr);
    }

} // namespace chronon3d::backends::vulkan
