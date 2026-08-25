#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_debug_context.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace chronon3d::backends::vulkan {

namespace {

bool parse_env_bool(const char* name, bool fallback = false) noexcept {
    const char* val = std::getenv(name);
    if (!val) return fallback;
    return std::strcmp(val, "1") == 0 ||
           std::strcmp(val, "true") == 0 ||
           std::strcmp(val, "TRUE") == 0 ||
           std::strcmp(val, "on") == 0 ||
           std::strcmp(val, "ON") == 0;
}

} // namespace

VulkanDebugConfig VulkanDebugConfig::from_environment() noexcept {
    VulkanDebugConfig cfg{};
    cfg.enable_validation = parse_env_bool("CHRONON3D_VULKAN_VALIDATION", false);
    cfg.enable_sync_validation = parse_env_bool("CHRONON3D_VULKAN_SYNC_VALIDATION", false);
    cfg.enable_gpu_assisted = parse_env_bool("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION", false);
    cfg.enable_debug_names = parse_env_bool("CHRONON3D_VULKAN_DEBUG_NAMES", true);
    cfg.fail_on_error = parse_env_bool("CHRONON3D_VULKAN_VALIDATION_FAIL_ON_ERROR", true);
    cfg.fail_on_warning = parse_env_bool("CHRONON3D_VULKAN_VALIDATION_FAIL_ON_WARNING", false);
    return cfg;
}

VulkanDebugContext::~VulkanDebugContext() {
    shutdown();
}

void VulkanDebugContext::configure_instance_requirements(
    std::vector<const char*>& enabled_layers,
    std::vector<const char*>& enabled_extensions,
    const VulkanDebugConfig& config)
{
    config_ = config;

    // 1. Enumerate available layers
    std::uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    if (layer_count > 0) {
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
    }

    // 2. Enumerate available instance extensions
    std::uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    if (ext_count > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_exts.data());
    }

    const auto layer_exists = [&](const char* name) {
        return std::any_of(available_layers.begin(), available_layers.end(),
            [name](const VkLayerProperties& p) {
                return std::strcmp(p.layerName, name) == 0;
            });
    };

    const auto ext_exists = [&](const char* name) {
        return std::any_of(available_exts.begin(), available_exts.end(),
            [name](const VkExtensionProperties& p) {
                return std::strcmp(p.extensionName, name) == 0;
            });
    };

    if (config_.enable_validation) {
        const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
        if (layer_exists(kValidationLayer)) {
            enabled_layers.push_back(kValidationLayer);
            validation_active_ = true;
            spdlog::info("[VulkanDebugContext] Enabled validation layer: {}", kValidationLayer);
        } else {
            spdlog::warn("[VulkanDebugContext] Validation layer '{}' requested but not found on system.", kValidationLayer);
        }
    }

    if (config_.enable_validation || config_.enable_debug_names) {
        if (ext_exists(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debug_utils_active_ = true;
            spdlog::debug("[VulkanDebugContext] Enabled extension: {}", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }

    if (config_.enable_validation &&
        (config_.enable_sync_validation || config_.enable_gpu_assisted) &&
        ext_exists(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
        enabled_extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }
}

void VulkanDebugContext::initialize(VkInstance instance, const VulkanDebugConfig& config) {
    instance_ = instance;
    config_ = config;

    if (!debug_utils_active_ || instance_ == VK_NULL_HANDLE) {
        return;
    }

    auto pfn_create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    pfn_destroy_debug_utils_messenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));

    if (pfn_create && validation_active_) {
        VkDebugUtilsMessengerCreateInfoEXT create_info{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            nullptr,
            0,
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            &VulkanDebugContext::debug_callback,
            this
        };

        const VkResult res = pfn_create(instance_, &create_info, nullptr, &messenger_);
        if (res == VK_SUCCESS) {
            spdlog::info("[VulkanDebugContext] Vulkan Debug Messenger initialized successfully.");
        } else {
            spdlog::warn("[VulkanDebugContext] Failed to create Vulkan Debug Messenger (VkResult={})", static_cast<int>(res));
        }
    }
}

void VulkanDebugContext::set_device(VkDevice device) {
    device_ = device;
    if (device_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE && debug_utils_active_) {
        pfn_set_debug_utils_object_name_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));
    }
}

void VulkanDebugContext::shutdown() {
    if (messenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE && pfn_destroy_debug_utils_messenger_) {
        pfn_destroy_debug_utils_messenger_(instance_, messenger_, nullptr);
        messenger_ = VK_NULL_HANDLE;
    }
    pfn_set_debug_utils_object_name_ = nullptr;
    pfn_destroy_debug_utils_messenger_ = nullptr;
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
}

void VulkanDebugContext::set_object_name(
    VkObjectType object_type,
    std::uint64_t object_handle,
    const char* name) const noexcept
{
    if (!pfn_set_debug_utils_object_name_ || device_ == VK_NULL_HANDLE || !name || object_handle == 0) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        nullptr,
        object_type,
        object_handle,
        name
    };
    pfn_set_debug_utils_object_name_(device_, &info);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugContext::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    auto* self = static_cast<VulkanDebugContext*>(user_data);
    if (!self || !callback_data) return VK_FALSE;

    const char* vuid = callback_data->pMessageIdName ? callback_data->pMessageIdName : "UNNAMED";
    const char* msg = callback_data->pMessage ? callback_data->pMessage : "";

    std::lock_guard<std::mutex> lock(self->report_mutex_);
    if (callback_data->pMessageIdName) {
        self->current_report_.vuids.emplace_back(vuid);
    }

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        self->current_report_.error_count++;
        self->current_report_.errors.emplace_back(std::string("[") + vuid + "] " + msg);
        spdlog::error("[Vulkan Validation ERROR: {}] {}", vuid, msg);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        self->current_report_.warning_count++;
        self->current_report_.warnings.emplace_back(std::string("[") + vuid + "] " + msg);
        spdlog::warn("[Vulkan Validation WARNING: {}] {}", vuid, msg);
    } else {
        self->current_report_.info_count++;
        spdlog::debug("[Vulkan Validation INFO: {}] {}", vuid, msg);
    }

    return VK_FALSE;
}

VulkanValidationReport VulkanDebugContext::report() const {
    std::lock_guard<std::mutex> lock(report_mutex_);
    return current_report_;
}

bool VulkanDebugContext::write_validation_artifact(
    const std::string& path,
    const std::string& gpu_name,
    const std::string& driver_version,
    const std::string& vulkan_version) const
{
    const auto r = report();
    nlohmann::json j;
    j["gpu_name"] = gpu_name;
    j["driver_version"] = driver_version;
    j["vulkan_version"] = vulkan_version;
    j["validation_active"] = validation_active_;
    j["debug_utils_active"] = debug_utils_active_;
    j["sync_validation"] = config_.enable_sync_validation;
    j["gpu_assisted"] = config_.enable_gpu_assisted;
    j["error_count"] = r.error_count;
    j["warning_count"] = r.warning_count;
    j["info_count"] = r.info_count;
    j["errors"] = r.errors;
    j["warnings"] = r.warnings;
    j["vuids"] = r.vuids;

    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << j.dump(2) << std::endl;
    return true;
}

void VulkanDebugContext::reset_report() {
    std::lock_guard<std::mutex> lock(report_mutex_);
    current_report_ = {};
}

} // namespace chronon3d::backends::vulkan
#endif
