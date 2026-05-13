#pragma once

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

[[nodiscard]] constexpr std::string_view to_string(SourceKind kind) {
    switch (kind) {
    case SourceKind::Shape:      return "shape";
    case SourceKind::Text:       return "text";
    case SourceKind::Image:      return "image";
    case SourceKind::Video:      return "video";
    case SourceKind::Layer:      return "layer";
    case SourceKind::Precomp:    return "precomp";
    case SourceKind::Adjustment: return "adjustment";
    }
    return "layer";
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

private:
    std::map<std::string, SourceDescriptor, std::less<>> m_sources;
};

} // namespace chronon3d::registry
