#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <optional>

namespace chronon3d::backends::vulkan {

struct VulkanDebugConfig {
    bool enable_validation{false};
    bool enable_sync_validation{false};
    bool enable_gpu_assisted{false};
    bool enable_debug_names{true};
    bool fail_on_error{true};
    bool fail_on_warning{false};

    static VulkanDebugConfig from_environment() noexcept;
};

struct VulkanValidationReport {
    std::uint64_t error_count{0};
    std::uint64_t warning_count{0};
    std::uint64_t info_count{0};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> vuids;
};

class VulkanDebugContext {
public:
    VulkanDebugContext() = default;
    ~VulkanDebugContext();

    VulkanDebugContext(const VulkanDebugContext&) = delete;
    VulkanDebugContext& operator=(const VulkanDebugContext&) = delete;

    /// Configures instance layers and extensions based on availability and config.
    void configure_instance_requirements(
        std::vector<const char*>& enabled_layers,
        std::vector<const char*>& enabled_extensions,
        const VulkanDebugConfig& config);

    /// Initializes debug messenger on the created instance.
    void initialize(VkInstance instance, const VulkanDebugConfig& config);

    /// Associates the logical device for debug utilities (e.g. object naming).
    void set_device(VkDevice device);

    /// Shuts down debug messenger and cleans up state.
    void shutdown();

    /// Assigns a human-readable debug name to a Vulkan object via VK_EXT_debug_utils.
    void set_object_name(
        VkObjectType object_type,
        std::uint64_t object_handle,
        const char* name) const noexcept;

    void set_image_name(VkImage image, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(image), name);
    }

    void set_buffer_name(VkBuffer buffer, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer), name);
    }

    void set_pipeline_name(VkPipeline pipeline, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline), name);
    }

    void set_semaphore_name(VkSemaphore semaphore, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(semaphore), name);
    }

    void set_fence_name(VkFence fence, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_FENCE, reinterpret_cast<std::uint64_t>(fence), name);
    }

    void set_command_buffer_name(VkCommandBuffer cmd, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(cmd), name);
    }

    void set_pipeline_layout_name(VkPipelineLayout layout, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<std::uint64_t>(layout), name);
    }

    void set_descriptor_set_layout_name(VkDescriptorSetLayout layout, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<std::uint64_t>(layout), name);
    }

    void set_descriptor_pool_name(VkDescriptorPool pool, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<std::uint64_t>(pool), name);
    }

    void set_descriptor_set_name(VkDescriptorSet desc_set, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(desc_set), name);
    }

    void set_image_view_name(VkImageView view, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view), name);
    }

    void set_sampler_name(VkSampler sampler, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(sampler), name);
    }

    void set_query_pool_name(VkQueryPool pool, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<std::uint64_t>(pool), name);
    }

    void set_command_pool_name(VkCommandPool pool, const char* name) const noexcept {
        set_object_name(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<std::uint64_t>(pool), name);
    }

    /// Writes a structured validation report artifact JSON to path.
    bool write_validation_artifact(
        const std::string& path,
        const std::string& gpu_name = "",
        const std::string& driver_version = "",
        const std::string& vulkan_version = "") const;

    /// Retrieves accumulated validation report.
    [[nodiscard]] VulkanValidationReport report() const;

    /// Clears accumulated messages.
    void reset_report();

    [[nodiscard]] bool is_validation_active() const noexcept {
        return validation_active_;
    }

    [[nodiscard]] bool is_debug_utils_active() const noexcept {
        return debug_utils_active_;
    }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data);

    VkInstance instance_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
    PFN_vkSetDebugUtilsObjectNameEXT pfn_set_debug_utils_object_name_{nullptr};
    PFN_vkDestroyDebugUtilsMessengerEXT pfn_destroy_debug_utils_messenger_{nullptr};

    VulkanDebugConfig config_{};
    bool validation_active_{false};
    bool debug_utils_active_{false};

    mutable std::mutex report_mutex_;
    VulkanValidationReport current_report_{};
};

} // namespace chronon3d::backends::vulkan
#endif
