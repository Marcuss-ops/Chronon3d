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
};
