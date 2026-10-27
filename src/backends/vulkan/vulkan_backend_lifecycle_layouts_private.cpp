// vulkan_backend_lifecycle_layouts_private.cpp — descriptor set layouts, pools and pipeline layouts.
// Split from vulkan_backend_lifecycle_private.cpp per issue #51;
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::init_descriptor_layouts() {
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
            {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        const VkDescriptorSetLayoutCreateInfo tile_raster_layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            7, tile_raster_bindings};
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

}

} // namespace chronon3d::backends::vulkan
