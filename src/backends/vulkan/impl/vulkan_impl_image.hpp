#pragma once

// Fragment included inside VulkanBackend::Impl. Keeps the image resource
// layout isolated from backend-wide state and lock declarations.
struct Image {
    VkImage image{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkFormat format{VK_FORMAT_R32G32B32A32_SFLOAT};
    std::uint32_t width{0};
    std::uint32_t height{0};
    runtime::TextAtlasEncoding text_atlas_encoding{
        runtime::TextAtlasEncoding::PremultipliedRGBA};
    bool initialized{false};
    bool exportable{false};
    bool cuda_timeline{false};
    std::shared_ptr<InteropGeneration> interop_generation{};
    VkSemaphore cuda_to_vulkan{VK_NULL_HANDLE};
    VkSemaphore vulkan_to_cuda{VK_NULL_HANDLE};
    std::uint64_t last_vulkan_to_cuda_value{0};
    std::optional<std::size_t> cuda_export_fence_slot{};
    bool cuda_export_standalone_fence{false};
    // True from the submit that signals V2C until the surface is re-acquired
    // for Vulkan (which only happens after the consumer's C2V wait). While it
    // is set the CUDA/NVENC consumer may still read this image, so the lazy
    // prune must retain the slot: a Vulkan fence does not order the external
    // consumer. Teardown (destroy_all) is after vkDeviceWaitIdle and the
    // media layer is gone, so it still reclaims unconditionally.
    bool export_handed_off{false};
};
