#include <chronon3d/render_graph/backend_registry.hpp>

#include <algorithm>
#include <exception>
#include <utility>

namespace chronon3d::graph {

namespace {

bool same_type(const BackendDescriptor& descriptor, BackendType type) noexcept {
    return descriptor.type == type;
}

} // namespace

bool BackendRegistry::register_backend(BackendType type,
                                       BackendCapabilities capabilities,
                                       BackendFactory factory) {
    if (!factory || contains(type)) return false;
    m_backends.push_back(BackendDescriptor{type, capabilities, std::move(factory)});
    return true;
}

bool BackendRegistry::contains(BackendType type) const noexcept {
    return descriptor(type) != nullptr;
}

const BackendDescriptor* BackendRegistry::descriptor(BackendType type) const noexcept {
    const auto it = std::find_if(m_backends.begin(), m_backends.end(),
                                 [type](const auto& item) { return same_type(item, type); });
    return it == m_backends.end() ? nullptr : &*it;
}

std::vector<BackendDescriptor> BackendRegistry::descriptors() const {
    return m_backends;
}

bool backend_capabilities_satisfy(const BackendCapabilities& capabilities,
                                  const BackendRequirements& requirements) noexcept {
    if (requirements.graphics && !capabilities.graphics) return false;
    if (requirements.compute && !capabilities.compute) return false;
    if (requirements.hardware_encode && !capabilities.hardware_encode) return false;
    if (requirements.min_texture_width > 0 &&
        capabilities.max_texture_width < requirements.min_texture_width) return false;
    if (requirements.min_texture_height > 0 &&
        capabilities.max_texture_height < requirements.min_texture_height) return false;
    if (requirements.min_device_memory_bytes > 0 &&
        capabilities.device_memory_bytes < requirements.min_device_memory_bytes) return false;
    return true;
}

Result<std::unique_ptr<RenderBackend>, BackendResolveError>
BackendResolver::resolve(BackendPreference preference,
                         const BackendRequirements& requirements) const {
    std::vector<BackendType> candidates;
    switch (preference) {
        case BackendPreference::Software:
            candidates = {BackendType::Software};
            break;
        case BackendPreference::GPU:
            candidates = {BackendType::Vulkan};
            break;
        case BackendPreference::Auto:
            candidates = {BackendType::Vulkan, BackendType::Software};
            break;
    }

    bool registered = false;
    for (const auto type : candidates) {
        const auto* item = m_registry.descriptor(type);
        if (!item) continue;
        registered = true;
        if (!backend_capabilities_satisfy(item->capabilities, requirements)) continue;
        try {
            auto backend = item->factory();
            if (backend) {
                return Result<std::unique_ptr<RenderBackend>, BackendResolveError>(std::move(backend));
            }
        } catch (const std::exception& error) {
            return Result<std::unique_ptr<RenderBackend>, BackendResolveError>(BackendResolveError{
                BackendResolveErrorCode::FactoryFailed, type,
                std::string{"backend factory failed for "} + backend_type_name(type) + ": " + error.what()});
        } catch (...) {
            return Result<std::unique_ptr<RenderBackend>, BackendResolveError>(BackendResolveError{
                BackendResolveErrorCode::FactoryFailed, type,
                std::string{"backend factory failed for "} + backend_type_name(type) + ": unknown exception"});
        }
        return Result<std::unique_ptr<RenderBackend>, BackendResolveError>(BackendResolveError{
            BackendResolveErrorCode::FactoryFailed, type,
            std::string{"backend factory returned nullptr for "} + backend_type_name(type)});
    }

    const auto requested = preference == BackendPreference::GPU
        ? BackendType::Vulkan : BackendType::Software;
    const auto code = registered ? BackendResolveErrorCode::NoMatchingBackend
                                 : BackendResolveErrorCode::NotRegistered;
    return Result<std::unique_ptr<RenderBackend>, BackendResolveError>(BackendResolveError{
        code, requested,
        std::string{"no backend satisfies preference="} + backend_preference_name(preference)});
}

const char* backend_type_name(BackendType type) noexcept {
    switch (type) {
        case BackendType::Software: return "software";
        case BackendType::Vulkan: return "vulkan";
    }
    return "unknown";
}

const char* backend_preference_name(BackendPreference preference) noexcept {
    switch (preference) {
        case BackendPreference::Auto: return "auto";
        case BackendPreference::Software: return "software";
        case BackendPreference::GPU: return "gpu";
    }
    return "unknown";
}

} // namespace chronon3d::graph
