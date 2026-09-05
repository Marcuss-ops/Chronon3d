// vulkan_descriptor_arena_private.cpp — VulkanBackend::Impl descriptor set
// allocation and descriptor writes (pass, glow, text, layer, tile sets).
//
// Image/buffer lifecycle lives in vulkan_backend_resources_private.cpp,
// synchronization emission in vulkan_backend_sync_private.cpp and the CUDA
// export path in vulkan_backend_cuda_export_private.cpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::ensure_descriptor_set() {
    if (descriptor_set != VK_NULL_HANDLE) return;
    const VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        descriptor_pool, 1, &descriptor_layout};
    check(vkAllocateDescriptorSets(device, &allocation, &descriptor_set),
          "vkAllocateDescriptorSets");
}

void VulkanBackend::Impl::bind_descriptors(const Image& destination,
                                           const Image& source) {
    ensure_descriptor_set();
    write_descriptors(descriptor_set, destination, source);
}

void VulkanBackend::Impl::write_descriptors(VkDescriptorSet set,
                                            const Image& destination,
                                            const Image& source) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo src_info{
        VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_fill_rect_descriptors(
    VkDescriptorSet set, const Image& destination) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
}

void VulkanBackend::Impl::bind_fill_rect_descriptors(const Image& destination) {
    ensure_descriptor_set();
    write_fill_rect_descriptors(descriptor_set, destination);
}

void VulkanBackend::Impl::write_matte_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& target, const Image& matte) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo target_info{
        VK_NULL_HANDLE, target.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo matte_info{
        VK_NULL_HANDLE, matte.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &target_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &matte_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_text_run_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& atlas, VkBuffer instance_buffer) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo atlas_info{
        VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_layer_batch_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& source, VkBuffer instance_buffer) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo src_info{
        VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_text_batch_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& atlas, VkBuffer glyph_buffer, VkBuffer run_buffer) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo atlas_info{
        VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo glyph_info{glyph_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo run_info{run_buffer, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &run_info, nullptr}};
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

VkDescriptorSet VulkanBackend::Impl::ensure_glow_descriptor_set(std::size_t index) {
    auto& set = glow_descriptor_sets[index];
    if (set != VK_NULL_HANDLE) return set;
    const VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        descriptor_pool, 1, &descriptor_layout};
    check(vkAllocateDescriptorSets(device, &allocation, &set),
          "vkAllocateDescriptorSets(glow)");
    return set;
}

VkCommandBuffer VulkanBackend::Impl::active_command_buffer() const noexcept {
    return frame_batch.command_buffers[frame_batch.next_slot];
}

VkDescriptorSet VulkanBackend::Impl::allocate_pass_descriptor_set() {
    const auto set =
        frame_batch.descriptor_allocators[frame_batch.next_slot].allocate();
    frame_batch.descriptor_sets.push_back(set);
    ++stats.descriptor_allocations;
    return set;
}

VkDescriptorSet VulkanBackend::Impl::allocate_text_tile_bin_descriptor_set() {
    if (!frame_batch.active) {
        throw std::logic_error("text tile bin requires an active frame batch");
    }
    return frame_batch.text_tile_bin_allocators[frame_batch.next_slot].allocate();
}

VkDescriptorSet VulkanBackend::Impl::allocate_text_tile_raster_descriptor_set() {
    if (!frame_batch.active) {
        throw std::logic_error("text tile raster requires an active frame batch");
    }
    return frame_batch.text_tile_raster_allocators[frame_batch.next_slot].allocate();
}

void VulkanBackend::Impl::write_text_tile_bin_descriptors(
    VkDescriptorSet set, VkBuffer glyph_buffer, VkBuffer run_buffer,
    VkBuffer tile_counts, VkBuffer tile_indices) {
    const VkDescriptorBufferInfo glyph{glyph_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo runs{run_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo counts{tile_counts, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo indices{tile_indices, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &runs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &counts, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indices, nullptr}};
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_text_tile_raster_descriptors(
    VkDescriptorSet set, const Image& destination, const Image& atlas,
    VkBuffer glyph_buffer, VkBuffer run_buffer,
    VkBuffer tile_counts, VkBuffer tile_indices) {
    const VkDescriptorImageInfo dst{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo atlas_info{
        VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo glyph{glyph_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo runs{run_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo counts{tile_counts, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo indices{tile_indices, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &runs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &counts, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indices, nullptr}};
    vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
}

} // namespace chronon3d::backends::vulkan
