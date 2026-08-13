#pragma once

#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::graph {

struct BackendRequirements {
    bool graphics{false};
    bool compute{false};
    bool hardware_encode{false};
    std::uint32_t min_texture_width{0};
    std::uint32_t min_texture_height{0};
    std::uint64_t min_device_memory_bytes{0};
};

enum class BackendResolveErrorCode {
    NotRegistered,
    NoMatchingBackend,
    FactoryFailed,
};

struct BackendResolveError {
    BackendResolveErrorCode code{BackendResolveErrorCode::NoMatchingBackend};
    BackendType requested{BackendType::Software};
    std::string message{};
};

using BackendFactory = std::function<std::unique_ptr<RenderBackend>()>;

struct BackendDescriptor {
    BackendType type{BackendType::Software};
    BackendCapabilities capabilities{};
    BackendFactory factory{};
};

/// Process-local registry.  Ownership of backend instances remains with the
/// caller, while factories are retained for the lifetime of the registry.
class BackendRegistry {
public:
    bool register_backend(BackendType type,
                          BackendCapabilities capabilities,
                          BackendFactory factory);

    [[nodiscard]] bool contains(BackendType type) const noexcept;
    [[nodiscard]] const BackendDescriptor* descriptor(BackendType type) const noexcept;
    [[nodiscard]] std::vector<BackendDescriptor> descriptors() const;

private:
    std::vector<BackendDescriptor> m_backends;
};

class BackendResolver {
public:
    explicit BackendResolver(const BackendRegistry& registry) noexcept
        : m_registry(registry) {}

    [[nodiscard]] Result<std::unique_ptr<RenderBackend>, BackendResolveError>
    resolve(BackendPreference preference,
            const BackendRequirements& requirements = {}) const;

private:
    const BackendRegistry& m_registry;
};

[[nodiscard]] bool backend_capabilities_satisfy(
    const BackendCapabilities& capabilities,
    const BackendRequirements& requirements) noexcept;

[[nodiscard]] const char* backend_type_name(BackendType type) noexcept;
[[nodiscard]] const char* backend_preference_name(BackendPreference preference) noexcept;

} // namespace chronon3d::graph
