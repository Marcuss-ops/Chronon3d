#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

namespace chronon3d::registry {

enum class SourceKind {
    Shape,
    Text,
    Image,
    Video,
    Layer,
    Precomp,
    Adjustment,
};

[[nodiscard]] inline std::string to_string(SourceKind kind) {
    return enum_utils::enum_name_lower_snake(kind);
}

struct SourceDescriptor {
    std::string id;
    std::string display_name;
    SourceKind  kind{SourceKind::Layer};
    std::string description;
    bool        builtin{false};
    bool        temporal{false};
    bool        composable{true};
};

class SourceRegistry {
public:
    SourceRegistry();

    void register_source(SourceDescriptor descriptor);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const SourceDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<SourceDescriptor> list() const;

    /// Clear all registered sources (used in tests for clean reset).
    void clear();

private:
    std::map<std::string, SourceDescriptor, std::less<>> m_sources;
};

} // namespace chronon3d::registry
